// OWNER: TESTS lane, round 6 step 10. The step-10 acceptance cases (included by tests/s10_tests.cpp). Pass marks: STEP_ORDER step 10
// (desktop: MV-11, SIM-18, SIM-25, the §4.11 permission units for every case, the teleport INV checks, FPS-1; plus the in-game
// marks the desktop can stand in for: sc18, sc13, sc5), ARCH §3.9.5 / §4.10 / §4.11 / §4.13 / §7.3 (MV-11, MV-12, MV-15, MV-20,
// MV-23, SIM-18, SIM-23, SIM-25, SIM-26, WA-30, INV-2, INV-11, INV-13) and the §7.7 rows this step owns (U07 U10 U14 U16 U26 U45
// U52 U54 U56 U57 U61 U63, G2 G5), LEAD 10-14 (never latched in a wall > 3 s), LEAD 10-16 (no idle-under-fire stretch > 2 s).
#include "s10_api.h"
#include "s10_brain.h"  // the shipped path: the REAL old brain + the step-10 Pilot (s3_sim.h world)
#include "s10_sim.h"
#include "s9_api.h"
#include "s9_world.h"

#include <functional>
#include <map>
#include <random>
#include <set>

namespace s10_tests
{
	using namespace s10;
	namespace A = s9api;
	namespace T = s10api;
	using s9::kClosed;
	using s9::kMaster;
	using s9::kOpen;

	inline void ApplySets(const std::vector<std::string>& a_sets)
	{
		for (const auto& s : a_sets) {
			const auto eq = s.find('=');
			if (eq == std::string::npos) continue;
			sp::cfg::SetTunable(s.substr(0, eq), std::strtod(s.c_str() + eq + 1, nullptr));
		}
	}

	// ------------------------------------------------------------------------------------------------ what runs
	inline std::shared_ptr<A::IStack> Stack()
	{
#if S9_HAVE_CORESTACK
		if (!g_refStack) return A::MakeCoreStack();
#endif
		return std::make_shared<A::RefStack>();
	}
	inline std::shared_ptr<T::ITele> Tele()
	{
#if S10_HAVE_CORETELE
		if (!g_forceRef) return T::MakeCoreTele();
#endif
		return T::MakeRefTele();
	}
	inline bool CoreTeleRuns() { return T::CoreTeleBuilt() && !g_forceRef; }
	inline const char* PolicyTag() { return CoreTeleRuns() ? "core" : "REFERENCE"; }
	inline Cfg C(double fps = 60)
	{
		Cfg c;
		c.fps = fps;
		c.stack = Stack();
		c.tele = Tele();
		return c;
	}
	inline Cfg Jit(double fps)
	{
		Cfg c = C(fps);
		c.jitter = 0.2;
		c.pumpsMin = 2.0;
		c.pumpsMax = 3.4;
		return c;
	}
	inline int Seeds(int full, int quick) { return g_seeds > 0 ? g_seeds : (g_quick ? quick : full); }

	// ------------------------------------------------------------------------------------------------ pooled invariants
	struct Pool
	{
		int runs{ 0 }, frames{ 0 }, relocs{ 0 }, bad{ 0 }, unexplained{ 0 }, seenNotLow{ 0 }, torsoIn{ 0 }, nonFinite{ 0 }, defend{ 0 }, lost{ 0 }, wedged{ 0 };
		double              unshMax{ 0 }, iufMax{ 0 }, stillMax{ 0 };
		std::string         firstBad{}, unshScene{}, iufScene{};
		std::vector<double> stuck{}, costUs{};
		std::string         stuckWorst{};
		double              stuckWorstT{ 0 };
		void Add(const Res& r)
		{
			++runs;
			frames += r.frames;
			relocs += static_cast<int>(r.relocs.size());
			bad += r.bad;
			unexplained += r.unexplained;
			seenNotLow += r.seenNotLowHpDefend;
			torsoIn += r.torsoIn5;
			nonFinite += r.finite ? 0 : 1;
			defend += r.defendJumps;
			lost += r.lostTransfers;
			wedged += r.wedged;
			if (r.unshieldedMax > unshMax) unshMax = r.unshieldedMax, unshScene = r.name;
			if (r.iufMax > iufMax) iufMax = r.iufMax, iufScene = r.name;
			stillMax = std::max(stillMax, r.stillHitMax);
			for (const auto& row : r.relocs)
				if (!row.bad.empty() && firstBad.empty()) {
					char b[400];
					std::snprintf(b, sizeof b, "%s t=%.2f %s: %s", r.name.c_str(), row.e.t, T::KindName(row.e.kind), row.bad.c_str());
					firstBad = b;
				}
			for (double s : r.stuck) {
				stuck.push_back(s);
				if (s > stuckWorstT) stuckWorstT = s, stuckWorst = r.name;
			}
			for (size_t i = 0; i < r.costUs.size(); i += 5) costUs.push_back(r.costUs[i]);
		}
	};
	inline Pool& P()
	{
		static Pool p;
		return p;
	}
	inline Res Run(const Scene& s, Cfg c, bool pool = true)
	{
		if (g_debug && s.name == g_debug) c.verbose = true;
		Res r = RunScene(s, c);
		if (pool) P().Add(r);
		return r;
	}
	inline std::string Brief(const Res& r)
	{
		char b[1100];
		std::snprintf(b, sizeof b,
			"kills %d/%d | relocs %zu (defend %d lost %d wedged %d papyrus %d unexplained %d) bad %d seen %d (not low-hp defend %d) | shield first %.2f s, %.1f s total, "
			"unshielded max %.2f s | player min %.2f downs %d | iuf max %.2f still-hit max %.2f | stuck n %zu p95 %.2f max %.2f | held %.2f | torsoIn %.1f | back %.2f s | %.1f s",
			r.kills, r.foes, r.relocs.size(), r.defendJumps, r.lostTransfers, r.wedged, r.papyrus, r.unexplained, r.bad, r.seenRelocs, r.seenNotLowHpDefend, r.firstShieldAt, r.shieldS,
			r.unshieldedMax, r.playerMin, r.playerDowns, r.iufMax, r.stillHitMax, r.stuck.size(), Pct(r.stuck, 0.95), Pct(r.stuck, 1.0), r.heldAtWall, r.torsoInMax, r.backWithPlayerAt,
			r.seconds);
		return b;
	}
	inline const char* FirstBad(const Res& r)
	{
		for (const auto& row : r.relocs)
			if (!row.bad.empty()) return row.bad.c_str();
		return "-";
	}
	inline double DeadAt(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		return (it == r.foeEnd.end() || it->second.deadAt < 0) ? 1e9 : it->second.deadAt;
	}
	inline const Foe* FoeEnd(const Res& r, std::uint32_t id)
	{
		auto it = r.foeEnd.find(id);
		return it == r.foeEnd.end() ? nullptr : &it->second;
	}

	// ================================================================================================ §4.11 permission units (TP)
	// The policy alone (RefTele, or the core's when wired) on synthetic frames at 60 Hz with a fake engine side.
	struct FakeW final : public T::ITeleWorld
	{
		bool  seenFrom{ false }, seenTo{ false }, blink{ true }, lost{ true }, floor{ true };
		Vec3  me{};
		int   calls{ 0 };
		bool Seen(const Vec3& p) override
		{
			++calls;
			return Length(p - me) < 1.0 ? seenFrom : seenTo;
		}
		bool BlinkSpot(const Vec3& tFeet, const Vec3&, const Vec3&, Vec3& out) override
		{
			out = tFeet + Vec3{ 85, 0, 0 };
			return blink;
		}
		bool LostSpot(const Vec3& pFeet, const Vec3& pView, Vec3& out) override
		{
			out = pFeet - pView * 420.0;
			return lost;
		}
		bool FloorNear(const Vec3& meFeet, double, Vec3& out) override
		{
			out = meFeet + Vec3{ 120, 0, 0 };
			return floor;
		}
	};
	struct UnitRun
	{
		std::vector<std::pair<double, T::Kind>> relocs{};
		int    shieldFrames{ 0 }, frames{ 0 };
		double firstShield{ -1 };
		double First(T::Kind k) const
		{
			for (const auto& [t, kk] : relocs)
				if (kk == k) return t;
			return -1;
		}
		int Count(T::Kind k) const
		{
			int n = 0;
			for (const auto& [t, kk] : relocs) n += kk == k;
			return n;
		}
	};
	// a jump-only band-D shooter 900 u from the player that hits the player every a_every s from t = 0 (a_every <= 0: never)
	inline T::Sit BaseSit(double hp)
	{
		T::Sit s;
		s.pFeet = { 0, 0, 0 };
		s.pEye = { 0, 0, 120 };
		s.pView = { 1, 0, 0 };
		s.hp = hp;
		s.meFeet = { 200, 300, 0 };
		s.meTorso = s.meFeet + Vec3{ 0, 0, 110 };
		s.target = 0xA0;
		T::FoeFact f;
		f.id = 0xA0;
		f.feet = { 900, 0, 0 };
		f.chest = { 900, 0, 92 };
		f.band = 4;
		f.reach = W::ReachKind::kJumpOnly;
		f.lineToPlayer = true;
		s.foes.push_back(f);
		return s;
	}
	using UnitScript = std::function<void(double, T::Sit&, FakeW&)>;
	inline UnitRun RunUnit(T::ITele& pol, double dur, T::Sit s, FakeW w, const UnitScript& fn, double fps = 60)
	{
		UnitRun u;
		pol.Reset();
		const double dt = 1.0 / fps;
		for (double t = 0; t < dur; t += dt) {
			s.now = t;
			s.wall = t;
			s.dt = dt;
			w.me = s.meFeet;
			if (fn) fn(t, s, w);
			T::Act a;
			pol.Frame(s, w, a);
			++u.frames;
			if (a.shield) {
				++u.shieldFrames;
				if (u.firstShield < 0) u.firstShield = t;
			}
			if (a.reloc != T::Kind::kNone) {
				u.relocs.push_back({ t, a.reloc });
				pol.Executed(a.reloc, s);
				if (a.reloc == T::Kind::kDefend)  // he lands by the target: it is reachable now (and the jump never repeats on it)
					for (auto& ff : s.foes)
						if (ff.id == a.target) ff.reach = W::ReachKind::kDirect, ff.cnlFails = 0;
				if (a.reloc == T::Kind::kLost || a.reloc == T::Kind::kWedged) {
					s.meSpace = s.pSpace;
					s.meFeet = a.to;
					s.wedged = false;
				}
			}
		}
		return u;
	}
	// hits on the player by foe 0 every a_every s from a_from (to a_until)
	inline UnitScript Hits(double a_every, double a_from = 0.0, double a_until = 1e9)
	{
		return [=](double t, T::Sit& s, FakeW&) {
			if (t >= a_from - 1e-9 && t < a_until && !s.foes.empty()) {
				const double k = std::floor((t - a_from) / a_every + 1e-9);
				s.foes[0].hitPlayerAt = std::max(s.foes[0].hitPlayerAt, a_from + k * a_every);
			}
		};
	}
	inline UnitScript Both(UnitScript a, UnitScript b)
	{
		return [=](double t, T::Sit& s, FakeW& w) {
			a(t, s, w);
			b(t, s, w);
		};
	}

	inline void TP()
	{
		if (!Want("TP")) return;
		if (!CoreTeleRuns()) {
			if (g_forceRef && T::CoreTeleBuilt()) Info("TP-core", "--ref: the TP rows below judge the ARCH REFERENCE policy (the core's is built; run without --ref for it)");
			else Missing("TP-core", "the core's step-10 teleport policy is not in this tree (core/pilot.h + core/pilot_bridge.h): the TP rows below judge the ARCH REFERENCE policy");
		}
		auto pol = Tele();
		const double fr = 1.0 / 60.0;
		// TP-01 shield first (hp 1.0, both ends unseen)
		{
			FakeW w;
			const UnitRun a = RunUnit(*pol, 5.0, BaseSit(1.0), w, Hits(0.5));
			T::Sit s0 = BaseSit(1.0);
			const UnitRun b = RunUnit(*pol, 5.0, s0, w, Hits(10.0, 0.0, 0.01));  // one hit at t = 0 only
			const double  ja = a.First(T::Kind::kDefend);
			Check("TP-01", ja >= 1.0 - 1e-9 && ja <= 1.0 + 0.1 + fr && a.Count(T::Kind::kDefend) == 1 && a.firstShield >= 0 && a.firstShield <= fr && b.Count(T::Kind::kDefend) == 0 &&
			                   b.shieldFrames > 0,
				"[%s] shield first: a sealed band-D shooter hitting every 0.5 s at 100 %%, both ends unseen: shield from %.3f s, the defend jump at %.3f s (want 1.0..1.1: never before 1.0 s of "
				"shield); hit only once at t 0 (no hit DURING the shield): %d jumps in 5 s (0), shield frames %d",
				PolicyTag(), a.firstShield, ja, b.Count(T::Kind::kDefend), b.shieldFrames);
		}
		// TP-02 the view rule
		{
			FakeW seen;
			seen.seenFrom = seen.seenTo = true;
			const UnitRun hi = RunUnit(*pol, 10.0, BaseSit(1.0), seen, Hits(0.5));
			const UnitRun mid = RunUnit(*pol, 5.0, BaseSit(0.45), seen, Hits(0.5));
			const UnitRun lo = RunUnit(*pol, 2.0, BaseSit(0.30), seen, Hits(0.5));
			FakeW half;
			half.seenFrom = true;  // the take-off alone is seen
			const UnitRun hf = RunUnit(*pol, 5.0, BaseSit(1.0), half, Hits(0.5));
			// seen until 2.0 s, then the player turns away: the jump within 0.1 s (+1 frame) of the turn, never before
			FakeW turn;
			const UnitRun tu = RunUnit(*pol, 5.0, BaseSit(1.0), turn, Both(Hits(0.5), [](double t, T::Sit&, FakeW& ww) {
				ww.seenFrom = ww.seenTo = t < 2.0;
			}));
			const double jm = mid.First(T::Kind::kDefend), jl = lo.First(T::Kind::kDefend), jt = tu.First(T::Kind::kDefend);
			Check("TP-02", hi.Count(T::Kind::kDefend) == 0 && jm >= 1.0 - 1e-9 && jm <= 1.1 + fr && jl >= 0 && jl <= 2 * fr && hf.Count(T::Kind::kDefend) == 0 && jt >= 2.0 - 1e-9 &&
			                   jt <= 2.1 + fr,
				"[%s] the view rule: seen at 100 %% -> %d jumps in 10 s (0, he keeps shielding: %d of %d frames); seen at 45 %% while hit -> jump at %.3f s (1.0..1.1: shield first, then "
				"in view); seen at 30 %% -> at %.3f s (at once); only the take-off seen at 100 %% -> %d (0); seen until 2.0 s then unseen -> at %.3f s (2.0..2.1: re-checked every 0.1 s)",
				PolicyTag(), hi.Count(T::Kind::kDefend), hi.shieldFrames, hi.frames, jm, jl, hf.Count(T::Kind::kDefend), jt);
		}
		// TP-03 the budget: 2 per 600 s wall clock at or above 35 %, free below
		{
			auto stream = [&](double hp, double dur, bool wallJump) {
				// a fresh sealed shooter after every jump (the old one is dead), each hitting every 0.5 s
				T::Sit s = BaseSit(hp);
				FakeW  w;
				UnitRun u;
				pol->Reset();
				const double dt = 1.0 / 60.0;
				std::uint32_t next = 0xA1;
				double wallOff = 0, spawnAt = 0;
				for (double t = 0; t < dur; t += dt) {
					s.now = t;
					s.wall = t + wallOff;
					s.dt = dt;
					w.me = s.meFeet;
					const bool live = t >= spawnAt;
					s.foes[0].alive = live;
					if (live) s.foes[0].hitPlayerAt = std::max(spawnAt, std::floor(t / 0.5) * 0.5);
					T::Act a;
					pol->Frame(s, w, a);
					if (a.reloc == T::Kind::kDefend) {
						u.relocs.push_back({ t, a.reloc });
						pol->Executed(a.reloc, s);
						// that shooter dies; a fresh one (another cage) starts shooting 3 s later
						s.foes[0].id = next;
						s.foes[0].hitPlayerAt = -1e9;
						s.target = next++;
						spawnAt = t + 3.0;
						if (wallJump && u.Count(T::Kind::kDefend) == 2) wallOff += 600.0;  // a long pause menu: 600 s of wall clock, no game time
					}
				}
				return u;
			};
			const UnitRun a40 = stream(0.40, 60.0, false);
			const UnitRun a30 = stream(0.30, 20.0, false);  // (a jump every ~3 s: free below 35 %)
			const UnitRun aw = stream(0.40, 20.0, true);
			Check("TP-03", a40.Count(T::Kind::kDefend) == 2 && a30.Count(T::Kind::kDefend) >= 5 && aw.Count(T::Kind::kDefend) >= 3,
				"[%s] the budget: a new sealed shooter after each jump for 60 s at 40 %% -> %d jumps (2 per 600 s); at 30 %% for 20 s -> %d (free below 35 %%: >= 5); at 40 %% with 600 s of WALL "
				"clock passing after the 2nd (a pause, no game time) -> %d (>= 3: the budget runs on the wall clock)",
				PolicyTag(), a40.Count(T::Kind::kDefend), a30.Count(T::Kind::kDefend), aw.Count(T::Kind::kDefend));
			// TP-15 (step 10 fix lane 1, REVIEW W2): the free jumps below 35 % never use the budget. Two emergency jumps at 30 %,
			// then a stimpak (40 %) well within 600 s: the next caged shooters still get the 2 budgeted jumps (before: refused
			// "budget" for up to 10 min, protection gone until the player was below 35 % again). And the other way round: 2
			// budgeted jumps at 40 %, then 30 %: free jumps go on (the budget never blocks below 35 %).
			auto mixed = [&](double hpFirst, double hpThen, int nFirst, double dur, int& nThen) {
				T::Sit s = BaseSit(hpFirst);
				FakeW  w;
				UnitRun u;
				pol->Reset();
				const double  dt = 1.0 / 60.0;
				std::uint32_t next = 0xA1;
				double        spawnAt = 0;
				nThen = 0;
				for (double t = 0; t < dur; t += dt) {
					s.now = t;
					s.wall = t;
					s.dt = dt;
					s.hp = u.Count(T::Kind::kDefend) < nFirst ? hpFirst : hpThen;
					w.me = s.meFeet;
					const bool live = t >= spawnAt;
					s.foes[0].alive = live;
					if (live) s.foes[0].hitPlayerAt = std::max(spawnAt, std::floor(t / 0.5) * 0.5);
					T::Act a;
					pol->Frame(s, w, a);
					if (a.reloc == T::Kind::kDefend) {
						if (u.Count(T::Kind::kDefend) >= nFirst) ++nThen;
						u.relocs.push_back({ t, a.reloc });
						pol->Executed(a.reloc, s);
						s.foes[0].id = next;
						s.foes[0].hitPlayerAt = -1e9;
						s.target = next++;
						spawnAt = t + 3.0;
					}
				}
				return u;
			};
			int             then40 = 0, then30 = 0;
			const UnitRun   f30 = mixed(0.30, 0.40, 2, 40.0, then40);
			const UnitRun   b40 = mixed(0.40, 0.30, 2, 40.0, then30);
			Check("TP-15", then40 == 2 && b40.Count(T::Kind::kDefend) - 2 == then30 && then30 >= 3,
				"[%s] the free jumps keep no budget: 2 jumps at 30 %%, then 40 %% (a stimpak) for the rest of 40 s -> %d more jumps (2: the budget is untouched; the bug: 0); "
				"2 jumps at 40 %%, then 30 %% -> %d more (>= 3: free below 35 %%); total %d / %d",
				PolicyTag(), then40, then30, f30.Count(T::Kind::kDefend), b40.Count(T::Kind::kDefend));
		}
		// TP-04 reachability (100 %, unseen, hit every 0.5 s, 5 s)
		{
			FakeW  w;
			T::Sit route = BaseSit(1.0);
			route.foes[0].reach = W::ReachKind::kRoute;
			T::Sit otherD = BaseSit(1.0);
			T::FoeFact o = otherD.foes[0];
			o.id = 0xB0;
			o.reach = W::ReachKind::kRoute;
			o.feet = { 500, 500, 0 };
			otherD.foes.push_back(o);
			T::Sit gc = BaseSit(1.0);
			T::FoeFact g = gc.foes[0];
			g.id = 0xC0;
			g.band = 2;
			g.reach = W::ReachKind::kDirect;
			gc.foes.push_back(g);
			T::Sit bothJ = BaseSit(1.0);
			T::FoeFact j2 = bothJ.foes[0];
			j2.id = 0xD0;
			j2.feet = { 800, 400, 0 };
			bothJ.foes.push_back(j2);
			const UnitRun ur = RunUnit(*pol, 5.0, route, w, Hits(0.5));
			const UnitRun uo = RunUnit(*pol, 5.0, otherD, w, Hits(0.5));
			const UnitRun ug = RunUnit(*pol, 5.0, gc, w, Hits(0.5));
			const UnitRun ub = RunUnit(*pol, 5.0, bothJ, w, Hits(0.5));
			Check("TP-04", ur.relocs.empty() && ur.shieldFrames == 0 && uo.relocs.empty() && uo.shieldFrames == 0 && ug.relocs.empty() && ug.shieldFrames == 0 &&
			                   ub.Count(T::Kind::kDefend) == 1 && ub.shieldFrames > 0,
				"[%s] reachability: the target reachable by route -> %zu relocations / %d shield frames (0 / 0); another band-D foe reachable -> %zu / %d (0 / 0: he fights it); a band-C foe "
				"reachable -> %zu / %d (0 / 0: INV-2, he fights it, no shield); two jump-only band-D foes -> %d jump(s), shield %d frames (1 / > 0)",
				PolicyTag(), ur.relocs.size(), ur.shieldFrames, uo.relocs.size(), uo.shieldFrames, ug.relocs.size(), ug.shieldFrames, ub.Count(T::Kind::kDefend), ub.shieldFrames);
		}
		// TP-05 the close-range law failed twice counts like jump only
		{
			FakeW  w;
			T::Sit two = BaseSit(1.0), one = BaseSit(1.0);
			two.foes[0].reach = one.foes[0].reach = W::ReachKind::kRoute;
			two.foes[0].cnlFails = 2;
			one.foes[0].cnlFails = 1;
			const UnitRun u2 = RunUnit(*pol, 5.0, two, w, Hits(0.5));
			const UnitRun u1 = RunUnit(*pol, 5.0, one, w, Hits(0.5));
			Check("TP-05", u2.Count(T::Kind::kDefend) == 1 && u1.relocs.empty(),
				"[%s] the close-range law: reach route but CNL failed twice -> %d jump (1, after the shield at %.2f s); failed once -> %zu relocations (0)", PolicyTag(),
				u2.Count(T::Kind::kDefend), u2.First(T::Kind::kDefend), u1.relocs.size());
		}
		// TP-06 the hit window: the target must have hit the player within 2 s (at 30 %: no shield needed)
		{
			FakeW  w;
			T::Sit old = BaseSit(0.30), fresh = BaseSit(0.30);
			old.foes[0].hitPlayerAt = -2.1;
			fresh.foes[0].hitPlayerAt = -1.9;
			const UnitRun uo = RunUnit(*pol, 1.0, old, w, nullptr);
			const UnitRun uf = RunUnit(*pol, 1.0, fresh, w, nullptr);
			Check("TP-06", uo.relocs.empty() && uf.Count(T::Kind::kDefend) == 1 && uf.First(T::Kind::kDefend) <= 2 * fr,
				"[%s] the hit window at 30 %%: last hit 2.1 s ago -> %zu relocations (0); 1.9 s ago -> jump at %.3f s (at once)", PolicyTag(), uo.relocs.size(), uf.First(T::Kind::kDefend));
		}
		// TP-07 tele.defend 0: never a defend jump (the shield still holds)
		{
			sp::cfg::SetTunable("tele.defend", 0.0);
			auto       p0 = Tele();
			FakeW      w, seen;
			seen.seenFrom = seen.seenTo = true;
			const UnitRun a = RunUnit(*p0, 5.0, BaseSit(1.0), w, Hits(0.5));
			const UnitRun b = RunUnit(*p0, 3.0, BaseSit(0.20), seen, Hits(0.5));
			const UnitRun c2 = RunUnit(*p0, 3.0, BaseSit(0.20), w, Hits(0.5));
			sp::cfg::SetTunable("tele.defend", 1.0);
			Check("TP-07", a.Count(T::Kind::kDefend) + b.Count(T::Kind::kDefend) + c2.Count(T::Kind::kDefend) == 0 && a.shieldFrames > 0 && c2.shieldFrames > 0,
				"[%s] tele.defend 0: defend jumps at 100 %% / 20 %% seen / 20 %% unseen = %d / %d / %d (0); shield frames %d / %d (> 0: the shield still holds)", PolicyTag(),
				a.Count(T::Kind::kDefend), b.Count(T::Kind::kDefend), c2.Count(T::Kind::kDefend), a.shieldFrames, c2.shieldFrames);
		}
		// TP-08 the lost transfer: the player in another space
		{
			auto other = [](double hp) {
				T::Sit s = BaseSit(hp);
				s.foes.clear();
				s.target = 0;
				s.pSpace = 2;
				s.meSpace = 1;
				return s;
			};
			FakeW w;
			const UnitRun u = RunUnit(*pol, 5.0, other(1.0), w, nullptr);
			FakeW fromSeen;
			fromSeen.seenFrom = true;  // the take-off in ANOTHER space: it means nothing to this camera
			const UnitRun uf = RunUnit(*pol, 5.0, other(1.0), fromSeen, nullptr);
			FakeW toSeen;
			const UnitRun ut = RunUnit(*pol, 6.0, other(1.0), toSeen, [](double t, T::Sit&, FakeW& ww) { ww.seenTo = t < 4.0; });
			FakeW none;
			none.lost = false;
			const UnitRun un = RunUnit(*pol, 5.0, other(1.0), none, nullptr);
			const double  t1 = u.First(T::Kind::kLost), tf = uf.First(T::Kind::kLost), tt = ut.First(T::Kind::kLost), tn = un.First(T::Kind::kLost);
			Check("TP-08", t1 > 3.0 - 1e-9 && t1 <= 3.1 + fr && u.Count(T::Kind::kLost) == 1 && tf > 3.0 - 1e-9 && tf <= 3.1 + fr && tt >= 4.0 - 1e-9 && tt <= 4.1 + fr && tn > 3.0 - 1e-9,
				"[%s] the lost transfer (another space): at %.3f s (> 3.0, <= 3.1), once (%d); the take-off in the other space 'seen' -> %.3f s (not judged); the landing seen until 4.0 s -> %.3f s "
				"(re-checked: 4.0..4.1); no landing found -> %.3f s (the Papyrus placement, out of view)",
				PolicyTag(), t1, u.Count(T::Kind::kLost), tf, tt, tn);
		}
		// TP-09 the lost transfer: far with no route progress for 30 s (same space)
		{
			auto far = [](double guard) {
				T::Sit s = BaseSit(1.0);
				s.foes.clear();
				s.target = 0;
				s.guardDist = guard;
				s.progressAt = 0;
				return s;
			};
			FakeW w;
			const UnitRun a = RunUnit(*pol, 31.0, far(8100), w, nullptr);
			const UnitRun b = RunUnit(*pol, 40.0, far(7900), w, nullptr);
			// 500 u of progress at 20 s (9000 -> 8500 u: still beyond 8000; ARCH 'route progress', the core counts >= 300 u)
			const UnitRun c3 = RunUnit(*pol, 52.0, far(9000), w, [](double t, T::Sit& s, FakeW&) {
				if (t >= 20.0 && s.guardDist > 8600) {
					s.guardDist = 8500;
					s.progressAt = 20.0;
				}
			});
			const double ta = a.First(T::Kind::kLost), tc = c3.First(T::Kind::kLost);
			Check("TP-09", ta >= 30.0 - 1e-9 && ta <= 30.1 + fr && b.relocs.empty() && tc >= 50.0 - 1e-9 && tc <= 50.1 + fr,
				"[%s] far with no progress: 8100 u -> lost at %.3f s (30.0..30.1); 7900 u for 40 s -> %zu relocations (0); progress at 20 s -> lost at %.3f s (50.0..50.1)", PolicyTag(), ta,
				b.relocs.size(), tc);
		}
		// TP-10 the wedged relocation: out of view, <= 1 per 60 s, only with no free point within 64 u
		{
			auto wed = [](bool freePt) {
				T::Sit s = BaseSit(1.0);
				s.foes.clear();
				s.target = 0;
				s.wedged = true;
				s.freeWithin64 = freePt;
				return s;
			};
			FakeW w;
			// wedged again right after each relocation (a bad landing): the 2nd only after 60 s
			const UnitRun a = RunUnit(*pol, 70.0, wed(false), w, [](double, T::Sit& s, FakeW&) { s.wedged = true; });
			const UnitRun b = RunUnit(*pol, 5.0, wed(true), w, nullptr);
			FakeW seen;
			seen.seenTo = true;
			const UnitRun c4 = RunUnit(*pol, 5.0, wed(false), seen, nullptr);
			const double  t1 = a.First(T::Kind::kWedged);
			double        t2 = -1;
			int           n = 0;
			for (const auto& [t, k] : a.relocs)
				if (k == T::Kind::kWedged && ++n == 2) t2 = t;
			Check("TP-10", t1 >= 0 && t1 <= 2 * fr && t2 >= t1 + 60.0 - 1e-9 && a.Count(T::Kind::kWedged) == 2 && b.relocs.empty() && c4.relocs.empty(),
				"[%s] wedged: first at %.3f s, the second at %.3f s (>= 60 s later), %d in 70 s (2); a free point within 64 u -> %zu (0: the tow); the landing seen -> %zu (0)", PolicyTag(), t1,
				t2, a.Count(T::Kind::kWedged), b.relocs.size(), c4.relocs.size());
		}
		// TP-11 carry / escort / down (EXTERNAL): nothing acts
		{
			FakeW  w;
			T::Sit s = BaseSit(0.20);
			s.external = true;
			s.pSpace = 2;
			s.wedged = true;
			s.freeWithin64 = false;
			const UnitRun u = RunUnit(*pol, 6.0, s, w, Hits(0.5));
			Check("TP-11", u.relocs.empty() && u.shieldFrames == 0, "[%s] EXTERNAL (carry / escort / down) with every condition true: %zu relocations, %d shield frames (0 / 0)", PolicyTag(),
				u.relocs.size(), u.shieldFrames);
		}
		// TP-13 no target: the shield, never a jump; TP-14 the health crossing 35 % mid-shield: the jump at the crossing
		{
			FakeW  w;
			T::Sit s = BaseSit(1.0);
			s.target = 0;
			const UnitRun u = RunUnit(*pol, 4.0, s, w, Hits(0.5));
			FakeW seen;
			seen.seenFrom = seen.seenTo = true;
			const UnitRun x = RunUnit(*pol, 3.0, BaseSit(0.36), seen, Both(Hits(0.5), [](double t, T::Sit& ss, FakeW&) { ss.hp = t < 0.5 ? 0.36 : 0.34; }));
			const double  tx = x.First(T::Kind::kDefend);
			Check("TP-13", u.relocs.empty() && u.shieldFrames > 0, "[%s] no target (band-D jump-only foes exist): %zu relocations (0), shield frames %d (> 0)", PolicyTag(), u.relocs.size(),
				u.shieldFrames);
			Check("TP-14", tx >= 0.5 - 1e-9 && tx <= 0.5 + 2 * fr, "[%s] seen, 36 %% -> 34 %% at 0.5 s into the shield: the jump at %.3f s (at the crossing: no shield-first, view free)",
				PolicyTag(), tx);
		}
		// TP-12 the ORACLE bites (every forbidden row is flagged; the permitted ones pass)
		{
			int bad = 0, n = 0;
			std::string first;
			auto expect = [&](const T::Executed& e, bool forbidden, const char* what) {
				++n;
				const bool flagged = !T::Judge(e).empty();
				if (flagged != forbidden) {
					++bad;
					if (first.empty()) first = what;
				}
			};
			T::Executed ok;
			ok.kind = T::Kind::kDefend;
			ok.tgtBandD = true;
			ok.tgtHitAgo = 0.3;
			ok.shieldHeld = 1.05;
			ok.hitDuringShield = true;
			ok.hp = 0.9;
			expect(ok, false, "a permitted unseen defend jump");
			T::Executed e = ok;
			e.shieldHeld = 0.6;
			expect(e, true, "shield 0.6 s at 90 %");
			e = ok;
			e.hitDuringShield = false;
			expect(e, true, "no hit during the shield");
			e = ok;
			e.seenTo = true;
			expect(e, true, "seen at 90 %");
			e.hp = 0.45;
			e.stillHit = true;
			expect(e, false, "seen at 45 % while hit");
			e = ok;
			e.hp = 0.3;
			e.shieldHeld = 0;
			e.hitDuringShield = false;
			e.seenFrom = true;
			e.stillHit = true;
			expect(e, false, "at 30 % at once, seen");
			e = ok;
			e.jumpsAboveFreeIn600 = 2;
			expect(e, true, "over the budget");
			e = ok;
			e.tgtReachable = true;
			expect(e, true, "a reachable target");
			e = ok;
			e.otherDReachable = true;
			expect(e, true, "another band-D reachable");
			e = ok;
			e.tgtHitAgo = 2.5;
			expect(e, true, "last hit 2.5 s ago");
			e = ok;
			e.teleDefend = false;
			expect(e, true, "tele.defend 0");
			T::Executed l;
			l.kind = T::Kind::kLost;
			l.otherSpaceFor = 3.2;
			expect(l, false, "a permitted lost transfer");
			T::Executed l2 = l;
			l2.otherSpaceFor = 1.0;
			expect(l2, true, "lost after 1.0 s");
			l2 = l;
			l2.seenTo = true;
			expect(l2, true, "lost landing seen");
			l2 = l;
			l2.sameSpaceFrom = true;
			l2.otherSpaceFor = 0;
			l2.guardDist = 9000;
			l2.noProgressFor = 31;
			expect(l2, false, "far lost");
			l2.noProgressFor = 10;
			expect(l2, true, "far lost after 10 s");
			T::Executed o;
			o.kind = T::Kind::kOther;
			expect(o, true, "an old-kind put-back");
			T::Executed wd;
			wd.kind = T::Kind::kWedged;
			wd.wedgedTruth = true;
			wd.to = { 200, 0, 0 };
			expect(wd, false, "a permitted wedged relocation");
			wd.sinceLastWedged = 30;
			expect(wd, true, "wedged twice in 30 s");
			Check("TP-12", bad == 0, "the oracle (ground-truth INV-11 judge) flags every forbidden row and passes every permitted one: %d of %d rows wrong (%s)", bad, n, first.empty() ? "-" : first.c_str());
		}
	}

	// ================================================================================================ scene builders
	inline void Box2(Level& lv, double x0, double y0, double x1, double y1, double h, const char* name = "room")
	{
		lv.Wall(x0, y0, x1, y0, 0, h, {}, 16, 208, name);
		lv.Wall(x0, y1, x1, y1, 0, h, {}, 16, 208, name);
		lv.Wall(x0, y0, x0, y1, 0, h, {}, 16, 208, name);
		lv.Wall(x1, y0, x1, y1, 0, h, {}, 16, 208, name);
	}
	inline Foe Shooter(std::uint32_t id, const Vec3& feet, double dmg = 3.0, double every = 0.5, double activeAt = 0.0, std::uint32_t space = 1)
	{
		Foe f;
		f.id = id;
		f.feet = feet;
		f.dmg = dmg;
		f.shotEvery = every;
		f.activeAt = activeAt;
		f.space = space;
		return f;
	}
	// a glass cage (bars for shots and sight, a wall for bodies) with a solid roof: a navmesh island, no gate - jump only
	inline void GlassCage(Level& lv, double x0, double y0, double x1, double y1, const char* name)
	{
		lv.Floor(x0 + 32, y0 + 32, x1 - 32, y1 - 32, 0, name);
		lv.Hole(x0 - 32, y0 - 32, x1 + 32, y0 + 32, "cage cut s");
		lv.Hole(x0 - 32, y1 - 32, x1 + 32, y1 + 32, "cage cut n");
		lv.Hole(x0 - 32, y0 - 32, x0 + 32, y1 + 32, "cage cut w");
		lv.Hole(x1 - 32, y0 - 32, x1 + 32, y1 + 32, "cage cut e");
		lv.w.Box({ x0 - 8, y0 - 8, 0 }, { x0 + 8, y1 + 8, 300 }, 0, "cage glass w", s8::kLGlass);
		lv.w.Box({ x1 - 8, y0 - 8, 0 }, { x1 + 8, y1 + 8, 300 }, 0, "cage glass e", s8::kLGlass);
		lv.w.Box({ x0 - 8, y0 - 8, 0 }, { x1 + 8, y0 + 8, 300 }, 0, "cage glass s", s8::kLGlass);
		lv.w.Box({ x0 - 8, y1 - 8, 0 }, { x1 + 8, y1 + 8, 300 }, 0, "cage glass n", s8::kLGlass);
		lv.w.Box({ x0 - 8, y0 - 8, 300 }, { x1 + 8, y1 + 8, 316 }, 2, "cage roof");
	}
	inline constexpr std::uint32_t kCageA = 0x00F10A01u, kCageB = 0x00F10A02u;
	// sc18 replica (ARCH 7.4, the Combat Zone cage shooter): an interior arena, a closed glass cage with a roof, 1 or 2 raiders inside
	// shooting the player; the player looking at the cage (a_look 1) or away (0)
	inline Scene Cage18(std::uint32_t seed, double hp, int a_look, bool two, const char* name = "sc18")
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = true;
		lv.Floor(-768, -1568, 3168, 1568, 0, "arena");
		Box2(lv, -800, -1600, 3200, 1600, 900, "arena walls");
		lv.w.Box({ -808, -1608, 900 }, { 3208, 1608, 916 }, 2, "arena ceiling");
		GlassCage(lv, 1600, -320, 2240, 320, "cage floor");
		lv.BuildNav();
		s.start = { 900, 300, 0 };
		s.playerFeet = { 600, -300, 0 };
		s.hp0 = hp;
		s.foes.push_back(Shooter(kCageA, { 1980, 120, 0 }, 2.5, 0.5, 0.5));
		if (two) s.foes.push_back(Shooter(kCageB, { 1940, -160, 0 }, 2.5, 0.6, 0.5));
		if (a_look == 1) s.look = { { 0, Look::kAtPoint, { 1920, 0, 100 } } };
		else s.look = { { 0, Look::kDir, { -1, -0.3, 0 } } };
		s.tEnd = 12.0;
		s.stopWhenAllDead = true;
		return s;
	}
	// MV-15: a sealed interior room (a glass pane in the seal, no doorway): the foe idle / shooting the player
	inline Scene Sealed15(std::uint32_t seed, double hp, int a_look, bool idle)
	{
		Scene s;
		s.name = "mv15";
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = true;
		lv.Floor(32, -480, 992, 480, 0, "hall");
		lv.Floor(1056, -480, 2016, 480, 0, "sealed");
		Box2(lv, 0, -512, 2048, 512, 320, "outer");
		lv.w.Box({ 1016, -512, 0 }, { 1032, -200, 320 }, 0, "seal s");
		lv.w.Box({ 1016, 200, 0 }, { 1032, 512, 320 }, 0, "seal n");
		lv.w.Box({ 1016, -200, 0 }, { 1032, 200, 320 }, 0, "seal pane", s8::kLGlass);
		lv.w.Box({ -8, -520, 320 }, { 2056, 520, 336 }, 2, "ceiling");
		lv.BuildNav();
		s.start = { 420, 150, 0 };
		s.playerFeet = { 160, -120, 0 };
		s.hp0 = hp;
		s.foes.push_back(Shooter(0x00F10F15u, { 1600, 40, 0 }, 2.5, 0.5, idle ? 1e9 : 0.5));
		if (a_look == 1) s.look = { { 0, Look::kAtPoint, { 1500, 0, 100 } } };
		else s.look = { { 0, Look::kDir, { -1, 0.1, 0 } } };
		s.tEnd = idle ? 6.0 : 10.0;
		s.stopWhenAllDead = true;
		return s;
	}
	// engaged: the attacker's first hit on the player -> shield / jump / going for it
	inline double Engaged(const Res& r, std::uint32_t id)
	{
		const Foe* f = FoeEnd(r, id);
		if (!f || f->firstHitPlayer < 0) return -1;
		return f->engagedAt < 0 ? 99.0 : f->engagedAt - f->firstHitPlayer;
	}
	inline double FirstHitPlayer(const Res& r, std::uint32_t id)
	{
		const Foe* f = FoeEnd(r, id);
		return f ? f->firstHitPlayer : -1;
	}

	// ================================================================================================ the cases
	// ---- sc18 / SIM-23 (locked + roof) / MV-15 / WA-30 / G2 / U45: the cage shooter at 100 / 40 / 30 %, the player looking or not
	inline void SC18()
	{
		if (!Want("SC18")) return;
		struct Row
		{
			double hp;
			int    look;
			bool   two;
		};
		const Row rows[] = { { 1.0, 0, false }, { 1.0, 1, false }, { 0.40, 1, false }, { 0.30, 1, false }, { 1.0, 0, true }, { 0.40, 1, true } };
		for (const Row& row : rows) {
			int         n = 0, bad = 0, engagedBad = 0, noJump = 0, earlyJump = 0, lowBad = 0, unshBad = 0, iufBad = 0, killedBad = 0;
			double      engMax = 0, low = 1, jMin = 1e9, jMax = -1, unsh = 0, iuf = 0;
			std::string worst;
			for (int seed = 1; seed <= Seeds(10, 3); ++seed) {
				Scene s = Cage18(static_cast<std::uint32_t>(seed), row.hp, row.look, row.two);
				Res   r = Run(s, C(60));
				++n;
				bad += r.bad;
				const double eng = Engaged(r, kCageA), fh = FirstHitPlayer(r, kCageA);
				engMax = std::max(engMax, eng);
				if (eng > 1.5) ++engagedBad;  // (-1 = it never hit the player: he engaged before it could)
				low = std::min(low, r.playerMin);
				if (row.hp >= 1.0 && !row.two && r.playerMin < 0.6) ++lowBad;  // (sc18 = one shooter: player >= 0.6)
				unsh = std::max(unsh, r.unshieldedMax);
				if (r.unshieldedMax > 0.25) ++unshBad;
				iuf = std::max(iuf, r.iufMax);
				if (r.iufMax > 2.0) ++iufBad;
				if (r.firstDefendAt >= 0 && fh >= 0) {
					const double after = r.firstDefendAt - fh;
					jMin = std::min(jMin, after);
					jMax = std::max(jMax, after);
				} else {
					++noJump;
				}
				// a jump at or above 35 % before 1.0 s of shield (the oracle's own clock, at the jump's own health)
				for (const auto& rr : r.relocs)
					if (rr.e.kind == T::Kind::kDefend && rr.e.hp >= 0.35 && rr.e.shieldHeld < 1.0 - 0.034) ++earlyJump;
				// after a permitted jump the foes die (the target kept, CHAIN at once)
				if (r.defendJumps > 0 && r.kills < r.foes) ++killedBad;
				if (worst.empty() || r.bad || eng > 1.5) worst = Brief(r) + " | first bad: " + FirstBad(r);
			}
			char id[64];
			std::snprintf(id, sizeof id, "SC18.%s%.0f%s", row.two ? "two." : "", row.hp * 100, row.look ? ".look" : ".away");
			// what each row must show (ARCH 4.11 / WA-30 / MV-15): 100 % looking -> never a jump (shield all along); 100 % away -> a jump
			// only after >= 1.0 s of shield with a hit in it (both ends out of view); 40 % looking -> after the shield, in view allowed;
			// 30 % -> at once (<= 0.35 s after the first hit)
			bool rowOk = true;
			if (row.hp >= 0.5 && row.look == 1) rowOk = noJump == n;
			else if (row.hp < 0.35) rowOk = noJump == 0 && jMax <= 0.35 + 1.0;  // (+ the stack's first none / jump answer <= 1.0 s)
			else if (row.hp < 0.5) rowOk = noJump == 0 && earlyJump == 0;
			else rowOk = earlyJump == 0;
			Check(id, bad == 0 && engagedBad == 0 && lowBad == 0 && unshBad == 0 && iufBad == 0 && killedBad == 0 && rowOk,
				"[%s] %d seeds: forbidden relocations %d (0), engaged max %.2f s (<= 1.5) in %d bad, player low %.2f%s, unshielded (INV-2) max %.2f s (<= 0.25), idle under fire max %.2f s "
				"(<= 2), defend jump %.2f..%.2f s after the first hit (no jump in %d of %d; %d before 1.0 s of shield), runs with a jump but a foe alive %d | %s",
				PolicyTag(), n, bad, engMax, engagedBad, low, row.hp >= 1.0 ? " (>= 0.60)" : "", unsh, iuf, jMin > 1e8 ? -1.0 : jMin, jMax, noJump, n, earlyJump, killedBad, worst.c_str());
		}
	}
	inline void MV15()
	{
		if (!Want("MV-15")) return;
		// idle: no shield, no jump; shooting at 100 % (away / looking) / 40 % looking / 30 %
		{
			Res r = Run(Sealed15(1, 1.0, 1, true), C(60));
			Check("MV-15.idle", r.relocs.empty() && r.shieldS == 0 && r.bad == 0, "[%s] a sealed room, the foe idle: relocations %zu, shield %.2f s (0 / 0) | %s", PolicyTag(), r.relocs.size(),
				r.shieldS, Brief(r).c_str());
		}
		struct Row
		{
			double      hp;
			int         look;
			const char* id;
		};
		for (const Row& row : { Row{ 1.0, 0, "MV-15.100away" }, Row{ 1.0, 1, "MV-15.100look" }, Row{ 0.40, 1, "MV-15.40look" }, Row{ 0.30, 1, "MV-15.30look" } }) {
			Res          r = Run(Sealed15(2, row.hp, row.look, false), C(60));
			const double fh = FirstHitPlayer(r, 0x00F10F15u);
			const double jump = r.firstDefendAt >= 0 && fh >= 0 ? r.firstDefendAt - fh : -1;
			const double sh = r.firstDefendAt >= 0 && r.firstShieldAt >= 0 ? r.firstDefendAt - r.firstShieldAt : -1;
			bool         ok = r.bad == 0 && r.unshieldedMax <= 0.25;
			if (row.hp >= 0.5 && row.look == 1) ok = ok && r.defendJumps == 0 && r.shieldS > 3.0;
			else if (row.hp < 0.35) ok = ok && jump >= 0 && jump <= 1.35;
			else if (row.hp < 0.5) ok = ok && jump >= 0 && sh >= 1.0 - 0.034;
			else ok = ok && (jump < 0 || sh >= 1.0 - 0.034) && r.playerMin >= 0.6;  // 100 %, unseen: a jump MAY come (after the shield), the player stays >= 0.6
			Check(row.id, ok, "[%s] sealed room shooter at %.0f %%, the player %s: shield first at %.2f s, held %.2f s before the jump, the jump %.2f s after the first hit (-1 = none), relocations %zu | %s",
				PolicyTag(), row.hp * 100, row.look ? "looking at the pane" : "looking away", r.firstShieldAt, sh, jump, r.relocs.size(), Brief(r).c_str());
		}
	}
	// ---- the budget in the loop: three sealed cages one after the other (WA-30 budget, G5)
	inline void Budget()
	{
		if (!Want("BUDGET")) return;
		auto three = [&](double hp, std::uint32_t seed) {
			Scene s;
			s.name = "budget";
			s.seed = seed;
			Level& lv = s.lv;
			lv.interior = true;
			lv.Floor(-1568, -1568, 2368, 1568, 0, "arena");
			Box2(lv, -1600, -1600, 2400, 1600, 900, "arena walls");
			lv.w.Box({ -1608, -1608, 900 }, { 2408, 1608, 916 }, 2, "arena ceiling");
			GlassCage(lv, 1400, -320, 2000, 320, "cage 1");
			GlassCage(lv, -320, 900, 320, 1500, "cage 2");
			GlassCage(lv, -1500, -320, -900, 320, "cage 3");
			lv.BuildNav();
			s.start = { 300, 300, 0 };
			s.playerFeet = { 0, 0, 0 };
			s.hp0 = hp;
			s.hpFloor = hp - 0.03;
			s.foes.push_back(Shooter(0x00F1B001u, { 1700, 100, 0 }, 1.0, 0.5, 0.5));
			s.foes.push_back(Shooter(0x00F1B002u, { 0, 1200, 0 }, 1.0, 0.5, 1e9));
			s.foes.push_back(Shooter(0x00F1B003u, { -1200, 0, 0 }, 1.0, 0.5, 1e9));
			s.look = { { 0, Look::kAtHim, {} } };
			s.script = [](double t, Level&, std::vector<Foe>& fs) {
				for (size_t k = 1; k < fs.size(); ++k)
					if (fs[k].activeAt > 1e8 && (!fs[k - 1].alive || t >= fs[k - 1].activeAt + 25.0) && fs[k - 1].activeAt < 1e8) fs[k].activeAt = t + 0.5;
			};
			s.tEnd = 80.0;
			s.stopWhenAllDead = true;
			return Run(s, C(60));
		};
		int j40 = 99, j30 = -1, bad = 0;
		std::string b40, b30;
		for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
			const Res a = three(0.40, static_cast<std::uint32_t>(seed));
			const Res b = three(0.30, static_cast<std::uint32_t>(seed));
			j40 = std::min(j40, a.defendJumps == 2 ? 2 : (a.defendJumps > 2 ? 100 + a.defendJumps : a.defendJumps));
			j30 = std::max(j30, b.defendJumps);
			bad += a.bad + b.bad;
			if (b40.empty() || a.defendJumps != 2) b40 = Brief(a);
			if (b30.empty() || b.defendJumps != 3) b30 = Brief(b);
		}
		Check("BUDGET", j40 == 2 && j30 == 3 && bad == 0,
			"[%s] three sealed shooters one after the other (80 s): at 40 %% -> %d defend jumps (2: tele.defendBudget 2 per 600 s), at 30 %% -> %d (3: free below 35 %%), forbidden %d | 40: %s | 30: %s",
			PolicyTag(), j40 > 99 ? j40 - 100 : j40, j30, bad, b40.c_str(), b30.c_str());
	}
	// ---- tele.defend 0: no defend jump at all, the shield still holds
	inline void Def0()
	{
		if (!Want("DEF0")) return;
		int    jumps = 0, relocs = 0;
		double shield = 1e9, eng = 0;
		std::string br;
		for (int seed = 1; seed <= Seeds(5, 2); ++seed) {
			Scene s = Cage18(static_cast<std::uint32_t>(seed), 0.30, 1, false, "def0");
			s.teleDefend = 0;
			sp::cfg::SetTunable("tele.defend", 0.0);
			Cfg c = C(60);
			sp::cfg::SetTunable("tele.defend", 1.0);
			s.tEnd = 8.0;
			Res r = Run(s, c);
			jumps += r.defendJumps;
			relocs += static_cast<int>(r.relocs.size());
			shield = std::min(shield, r.shieldS);
			eng = std::max(eng, Engaged(r, kCageA));
			br = Brief(r);
		}
		Check("DEF0", jumps == 0 && relocs == 0 && shield >= 4.0 && eng <= 1.5,
			"[%s] tele.defend 0 at 30 %%: defend jumps %d, relocations %d (0 / 0); the shield held >= %.1f s of 8 (>= 4), engaged %.2f s (<= 1.5) | %s", PolicyTag(), jumps, relocs, shield, eng, br.c_str());
	}
	// ---- a reachable shooter: no shield, no jump (INV-2: he fights it)
	inline void Reach()
	{
		if (!Want("REACH")) return;
		Scene s;
		s.name = "reach";
		Level& lv = s.lv;
		lv.interior = true;
		lv.Floor(32, -768, 2016, 768, 0, "hall");
		Box2(lv, 0, -800, 2048, 800, 400, "hall walls");
		lv.w.Box({ -8, -808, 400 }, { 2056, 808, 416 }, 2, "ceiling");
		lv.w.Box({ 1200, -200, 0 }, { 1260, 200, 140 }, 0, "crate");
		lv.BuildNav();
		s.start = { 300, 200, 0 };
		s.playerFeet = { 200, -200, 0 };
		s.foes.push_back(Shooter(0x00F1E001u, { 1600, 100, 0 }, 3.0, 0.5, 0.3));
		s.foes.push_back(Shooter(0x00F1E002u, { 1700, -500, 0 }, 3.0, 0.5, 0.3));
		s.look = { { 0, Look::kAtPoint, { 1600, 0, 100 } } };
		s.tEnd = 8.0;
		s.stopWhenAllDead = true;
		Res r = Run(s, C(60));
		Check("REACH", r.relocs.empty() && r.shieldS == 0 && r.kills == r.foes && r.iufMax <= 2.0,
			"[%s] two shooters in an open hall (reachable): relocations %zu, shield %.2f s (0 / 0), killed %d/%d by %.2f s, idle under fire max %.2f s | %s", PolicyTag(), r.relocs.size(), r.shieldS,
			r.kills, r.foes, std::max(DeadAt(r, 0x00F1E001u), DeadAt(r, 0x00F1E002u)), r.iufMax, Brief(r).c_str());
	}

	// ---- the lost transfer: a fast travel (SIM-26 / MV-20 / sc5 / U26) and a load door (MV-23)
	inline Scene Fields(std::uint32_t seed, int a_lookBack, int a_trips, const char* name)
	{
		Scene s;
		s.name = name;
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-3000, -3000, 3000, 3000, 0, "field A");
		lv.Floor(67000, -3000, 73000, 3000, 0, "field B");
		lv.Obstacle({ 1200, -600, 0 }, { 1260, 600, 300 }, "wall A");
		lv.Obstacle({ 71600, -200, 0 }, { 71660, 900, 300 }, "wall B");
		lv.BuildNav();
		s.start = { 200, 100, 0 };
		s.playerFeet = { 0, -100, 0 };
		s.playerPath = { { 600, -100, 0 } };
		s.playerSpeed = 200;
		// trip 1 at 1.0 s into field B (space 2), a fight 4 s after the arrival (within 10 s); trips 2 / 3 back and forth 20 s apart
		for (int k = 0; k < a_trips; ++k) {
			const bool toB = (k % 2) == 0;
			PlayerEv e;
			e.t = 1.0 + 20.0 * k;
			e.feet = toB ? Vec3{ 70000, 0, 0 } : Vec3{ -500, 300, 0 };
			e.space = toB ? 2u : 1u;
			e.path = { e.feet + Vec3{ 300, 50, 0 } };
			e.what = "fastTravel";
			s.events.push_back(e);
			const std::uint32_t base = 0x00F1F000u + static_cast<std::uint32_t>(k) * 16;
			const Vec3          o = toB ? Vec3{ 70000, 0, 0 } : Vec3{ -500, 300, 0 };
			s.foes.push_back(Shooter(base + 1, o + Vec3{ 1200, 600, 0 }, 1.5, 0.6, e.t + 4.0, e.space));
			s.foes.push_back(Shooter(base + 2, o + Vec3{ 1300, -500, 0 }, 1.5, 0.6, e.t + 4.0, e.space));
			if (a_lookBack) s.look.push_back({ e.t, Look::kDir, { -1, 0, 0 } });
			else s.look.push_back({ e.t, Look::kAlong, {} });
		}
		s.tEnd = 1.0 + 20.0 * a_trips;
		return s;
	}
	inline void Lost()
	{
		if (Want("SIM-26")) {
			int         bad = 0, n = 0, lostN = 0, wrongN = 0, fightBad = 0, stuckBad = 0, seen = 0;
			double      tMin = 1e9, tMax = -1, backMax = 0, killMax = 0;
			std::string br;
			for (int lookBack = 0; lookBack < 2; ++lookBack)
				for (int seed = 1; seed <= Seeds(4, 2); ++seed) {
					Scene s = Fields(static_cast<std::uint32_t>(seed), lookBack, 1, "sim26");
					s.tEnd = 15.0;
					Res r = Run(s, C(60));
					++n;
					bad += r.bad;
					seen += r.seenRelocs;
					lostN += r.lostTransfers;
					if (r.relocs.size() != 1 || r.lostTransfers != 1) ++wrongN;
					if (r.firstLostAt >= 0) tMin = std::min(tMin, r.firstLostAt - 1.0), tMax = std::max(tMax, r.firstLostAt - 1.0);
					const double back = r.afterArrival.empty() ? 99 : r.afterArrival[0];
					backMax = std::max(backMax, back < 0 ? 99.0 : back);
					// a fight within 10 s of the arrival: both raiders (active 4 s after it) dead by arrival + 10 s
					const double k1 = std::max(DeadAt(r, 0x00F1F001u), DeadAt(r, 0x00F1F002u)) - 1.0;
					killMax = std::max(killMax, k1);
					if (k1 > 10.0) ++fightBad;
					// 0 stuck rungs >= L2 (> 0.3 s) in the 10 s after the transfer
					for (double st : r.stuck)
						if (st > 0.3) ++stuckBad;
					if (br.empty() || r.bad || k1 > 10.0) br = Brief(r) + " | " + FirstBad(r);
				}
			Check("SIM-26", bad == 0 && wrongN == 0 && seen == 0 && tMin > 3.0 - 0.02 && tMax <= 3.3 && backMax <= 4.5 && fightBad == 0 && stuckBad == 0,
				"[%s] a fast travel 70 000 u into another space (+ the player looking back in half): %d runs, exactly one lost transfer each (%d wrong), seen %d, forbidden %d, at %.2f..%.2f s after "
				"the jump (3.0..3.3), back within 600 u of the player %.2f s after the arrival (<= 4.5), both raiders of the fight dead %.2f s after the arrival (<= 10), stuck rungs >= L2 %d (0) | %s",
				PolicyTag(), n, wrongN, seen, bad, tMin, tMax, backMax, killMax, stuckBad, br.c_str());
		}
		if (Want("MV-20")) {
			Scene s = Fields(1, 0, 3, "mv20");
			s.tEnd = 61.0;
			Res r = Run(s, C(60));
			Check("MV-20", r.bad == 0 && r.lostTransfers == 3 && r.relocs.size() == 3 && r.seenRelocs == 0 && r.kills == r.foes,
				"[%s] three fast travels A -> B -> A -> B (20 s apart), a fight after each: lost transfers %d (3), relocations %zu (3), seen %d (0), forbidden %d, foes dead %d/%d | %s",
				PolicyTag(), r.lostTransfers, r.relocs.size(), r.seenRelocs, r.bad, r.kills, r.foes, Brief(r).c_str());
		}
		if (Want("MV-23")) {
			// a load door: house A (space 1) -> house B (space 2, 40 000 u away)
			Scene s;
			s.name = "mv23";
			Level& lv = s.lv;
			lv.interior = true;
			lv.Floor(32, -480, 1504, 480, 0, "house A");
			Box2(lv, 0, -512, 1536, 512, 320, "house A walls");
			lv.w.Box({ -8, -520, 320 }, { 1544, 520, 336 }, 2, "house A ceiling");
			lv.Floor(40032, -768, 41504, 768, 0, "house B");
			Box2(lv, 40000, -800, 41536, 800, 320, "house B walls");
			lv.w.Box({ 39992, -808, 320 }, { 41544, 808, 336 }, 2, "house B ceiling");
			lv.BuildNav();
			s.start = { 400, 200, 0 };
			s.playerFeet = { 300, -200, 0 };
			s.playerPath = { { 1300, 300, 0 } };
			s.playerSpeed = 300;
			PlayerEv e;
			e.t = 3.0;
			e.feet = { 40400, 0, 0 };
			e.space = 2;
			e.path = { { 40800, 200, 0 } };
			e.what = "loadDoor";
			s.events.push_back(e);
			s.tEnd = 10.0;
			Res r = Run(s, C(60));
			const double t = r.firstLostAt - 3.0;
			Check("MV-23", r.bad == 0 && r.lostTransfers == 1 && r.relocs.size() == 1 && r.seenRelocs == 0 && t > 3.0 - 0.02 && t <= 3.3,
				"[%s] the player through a load door into house B: lost transfer %.2f s after (3.0..3.3), lost %d, relocations %zu (1), seen %d (0), forbidden %d | %s", PolicyTag(), t,
				r.lostTransfers, r.relocs.size(), r.seenRelocs, r.bad, Brief(r).c_str());
		}
		if (Want("LOST.far")) {
			// a walled + roofed courtyard 9000 u from the player, no exit (an island, no sky): no progress for 30 s -> the lost transfer
			auto yard = [](double dist, bool sealed, const char* name) {
				Scene s;
				s.name = name;
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-2000, -2000, dist + 2000, 2000, 0, "ground");
				if (sealed) {
					const double x0 = dist - 300, x1 = dist + 300;
					lv.Floor(x0 + 32, -268, x1 - 32, 268, 0, "yard");
					lv.Hole(x0 - 32, -332, x1 + 32, -268, "yard cut s");
					lv.Hole(x0 - 32, 268, x1 + 32, 332, "yard cut n");
					lv.Hole(x0 - 32, -332, x0 + 32, 332, "yard cut w");
					lv.Hole(x1 - 32, -332, x1 + 32, 332, "yard cut e");
					lv.w.Box({ x0 - 8, -308, 0 }, { x0 + 8, 308, 420 }, 0, "yard w");
					lv.w.Box({ x1 - 8, -308, 0 }, { x1 + 8, 308, 420 }, 0, "yard e");
					lv.w.Box({ x0 - 8, -308, 0 }, { x1 + 8, -292, 420 }, 0, "yard s");
					lv.w.Box({ x0 - 8, 292, 0 }, { x1 + 8, 308, 420 }, 0, "yard n");
					lv.w.Box({ x0 - 8, -308, 420 }, { x1 + 8, 308, 436 }, 2, "yard roof");
				}
				lv.BuildNav();
				s.start = { dist, 0, 0 };
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				return s;
			};
			Scene a = yard(9000, true, "lostfar");
			a.tEnd = 33.0;
			Res   ra = Run(a, C(60));
			Scene b = yard(6000, true, "lostnear");
			b.tEnd = 40.0;
			Res   rb = Run(b, C(60));
			Scene c3 = yard(9000, false, "farfly");
			c3.tEnd = 8.0;
			c3.reachPlayerR = 600;
			Res r3 = Run(c3, C(60));
			Check("LOST.far", ra.bad == 0 && ra.lostTransfers == 1 && ra.firstLostAt >= 30.0 - 0.1 && ra.firstLostAt <= 30.6 && ra.seenRelocs == 0 && rb.relocs.empty() && r3.relocs.empty() &&
			                      r3.backWithPlayerAt > 0 && r3.backWithPlayerAt <= 4.5,
				"[%s] far and stuck: sealed + roofed 9000 u away -> lost transfer at %.2f s (30.0..30.6), seen %d, forbidden %d; sealed 6000 u away -> %zu relocations in 40 s (0: the stuck ladder, "
				"never a put-back); 9000 u away in the open -> flown back at %.2f s (<= 4.5), %zu relocations (0) | far: %s | open: %s",
				PolicyTag(), ra.firstLostAt, ra.seenRelocs, ra.bad, rb.relocs.size(), r3.backWithPlayerAt, r3.relocs.size(), Brief(ra).c_str(), Brief(r3).c_str());
		}
	}

	// ---- wedged (MV-12 + the ARCH 4.11 wedged relocation)
	inline void Wedged()
	{
		if (!Want("MV-12")) return;
		auto block = [](double half, int a_look, const char* name) {
			Scene s;
			s.name = name;
			Level& lv = s.lv;
			lv.interior = false;
			lv.Floor(-2000, -2000, 4000, 2000, 0, "ground");
			lv.Obstacle({ 2000 - half, -half, 0 }, { 2000 + half, half, 300 }, "block");
			lv.BuildNav();
			s.start = { 2000 - (half > 100 ? 0.0 : half - 12.0), 0, 0 };
			s.insideStart = true;
			s.playerFeet = { 0, 0, 0 };
			s.look = { { 0, a_look ? Look::kAtPoint : Look::kDir, a_look ? Vec3{ 2000, 0, 100 } : Vec3{ -1, 0, 0 } } };
			s.tEnd = 6.0;
			return s;
		};
		Res a = Run(block(200, 0, "wedged-away"), C(60));
		Res b = Run(block(200, 1, "wedged-look"), C(60));
		Res c3 = Run(block(16, 1, "unwedge"), C(60));
		// (desktop: the step-8 Cage contacts push the TABS body out of a solid by themselves - the shipped path with [R6] Cage 0 uses
		// SimWalls' inside-solid rule -> the runtime's TeleWedgedFirst / WedgedRelocate, judged in game; here: safety + the numbers)
		Check("MV-12", a.bad == 0 && a.seenRelocs == 0 && a.unexplained == 0 && b.relocs.empty() && b.unexplained == 0 && c3.relocs.empty() && c3.unexplained == 0,
			"[%s] wedged in a 400 u block (no free point within 64 u): the player looking away -> wedged relocations %d (0 or 1, never seen: %d), forbidden %d, unexplained %d; the player looking "
			"at it -> %zu relocations (0: no larger move), unexplained %d; 12 u inside a 32 u slab (a free point within 64 u) -> relocations %zu, unexplained %d (0 / 0: the tow) | away: %s | look: %s | slab: %s",
			PolicyTag(), a.wedged, a.seenRelocs, a.bad, a.unexplained, b.relocs.size(), b.unexplained, c3.relocs.size(), c3.unexplained, Brief(a).c_str(), Brief(b).c_str(), Brief(c3).c_str());
	}

	// ---- SIM-18 / U54 / U57: the return after a far fight from a roofed ruin 3000 u away: flown back <= 3 s, 0 teleports
	inline void Ruin(double x, double y, Level& lv)
	{
		// an 800 x 800 ruin centred at (x, y), walls 300 u, a roof, one doorway (160 u) on its east side
		const double x0 = x - 400, x1 = x + 400, y0 = y - 400, y1 = y + 400;
		lv.Wall(x0, y0, x1, y0, 0, 300, {}, 16, 208, "ruin s");
		lv.Wall(x0, y1, x1, y1, 0, 300, {}, 16, 208, "ruin n");
		lv.Wall(x0, y0, x0, y1, 0, 300, {}, 16, 208, "ruin w");
		lv.Wall(x1, y0, x1, y1, 0, 300, { { y - 80, y + 80 } }, 16, 240, "ruin e");
		lv.w.Box({ x0 - 8, y0 - 8, 300 }, { x1 + 8, y1 + 8, 316 }, 2, "ruin roof");
		lv.Hole(x0 - 32, y0 - 32, x1 + 32, y0 + 32, "ruin cut s");
		lv.Hole(x0 - 32, y1 - 32, x1 + 32, y1 + 32, "ruin cut n");
		lv.Hole(x0 - 32, y0 - 32, x0 + 32, y1 + 32, "ruin cut w");
		lv.Hole(x1 - 32, y0 - 32, x1 + 32, y - 80, "ruin cut e1");
		lv.Hole(x1 - 32, y + 80, x1 + 32, y1 + 32, "ruin cut e2");
	}
	inline void SIM18()
	{
		if (!Want("SIM-18")) return;
		int         n = 0, relocs = 0, slow = 0;
		double      worst = 0, stuckMax = 0;
		std::string br;
		for (int seed = 1; seed <= Seeds(6, 2); ++seed) {
			Scene s;
			s.name = "sim18";
			s.seed = static_cast<std::uint32_t>(seed);
			Level& lv = s.lv;
			lv.interior = false;
			lv.Floor(-1500, -2000, 4500, 2000, 0, "ground");
			Ruin(3000, 0, lv);
			lv.BuildNav();
			s.start = { 2850.0 - 50.0 * seed, -150.0 + 60.0 * seed, 0 };
			s.playerFeet = { 0, 0, 0 };
			s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
			s.tEnd = 6.0;
			Res r = Run(s, C(60));
			++n;
			relocs += static_cast<int>(r.relocs.size());
			const double back = r.backWithPlayerAt < 0 ? 99.0 : r.backWithPlayerAt;
			worst = std::max(worst, back);
			if (back > 3.0) ++slow;
			stuckMax = std::max(stuckMax, Pct(r.stuck, 1.0));
			if (br.empty() || back > 3.0 || !r.relocs.empty()) br = Brief(r);
		}
		Check("SIM-18", relocs == 0 && slow == 0,
			"[%s] the return from inside a roofed ruin 3000 u away (its doorway facing away), %d starts: back within 300 u of the player in %.2f s worst (<= 3.0), relocations %d (0: flown, never "
			"placed), longest stuck %.2f s | %s",
			PolicyTag(), n, worst, relocs, stuckMax, br.c_str());
	}

	// ---- MV-11 (200 seeds): a U-shaped trap on the direct line; the stuck ladder (L1-L4), never a put-back
	inline Scene UTrap(std::uint32_t seed, double& ux0, double& ux1, double& uy0, double& uy1)
	{
		Rng    rng{ 0xABCDEFull + seed * 977ull };
		Scene  s;
		s.name = "mv11";
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = false;
		lv.Floor(-1600, -1600, 3200, 1600, 0, "ground");
		const double uw = rng.R(320, 640), ud = rng.R(256, 512), ux = rng.R(900, 1300);
		lv.Obstacle({ ux - ud, -uw * 0.5 - 16, 0 }, { ux, -uw * 0.5, 420 }, "U-south");
		lv.Obstacle({ ux - ud, uw * 0.5, 0 }, { ux, uw * 0.5 + 16, 420 }, "U-north");
		lv.Obstacle({ ux, -uw * 0.5 - 16, 0 }, { ux + 16, uw * 0.5 + 16, 420 }, "U-back");
		lv.BuildNav();
		ux0 = ux - ud, ux1 = ux + 16, uy0 = -uw * 0.5 - 16, uy1 = uw * 0.5 + 16;
		s.start = { rng.R(0, 300), rng.R(-120, 120), 0 };
		s.playerFeet = { ux + rng.R(400, 700), rng.R(-150, 150), 0 };
		s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
		s.reachPlayerR = 260;
		s.tEnd = 7.0;
		return s;
	}
	inline void MV11()
	{
		if (!Want("MV-11")) return;
		std::vector<double> esc, stuck;
		int                 relocs = 0, back = 0, n = 0;
		std::string         worst;
		double              worstT = -1;
		for (int seed = 1; seed <= Seeds(200, 20); ++seed) {
			double ux0, ux1, uy0, uy1;
			Scene  s = UTrap(static_cast<std::uint32_t>(seed), ux0, ux1, uy0, uy1);
			Res    r = Run(s, C(60));
			double inside = 0;
			for (size_t i = 1; i < r.track.size(); ++i) {
				const Vec3& p = r.track[i];
				if (p.x > ux0 && p.x < ux1 && p.y > uy0 && p.y < uy1) inside += r.trackT[i] - r.trackT[i - 1];
			}
			esc.push_back(inside);
			for (double x : r.stuck) stuck.push_back(x);
			relocs += static_cast<int>(r.relocs.size());
			if (r.backWithPlayerAt > 0) ++back;
			++n;
			if (inside > worstT) worstT = inside, worst = Brief(r);
		}
		Check("MV-11", Pct(esc, 0.95) <= 1.2 && Pct(esc, 1.0) <= 3.0 && relocs == 0 && Pct(stuck, 0.95) <= 1.2 && Pct(stuck, 1.0) <= 3.0,
			"[%s] a U-trap on the direct line to the player, %d seeds: time inside the U p95 %.2f / max %.2f s (<= 1.2 / 3.0), stuck episodes %zu p95 %.2f / max %.2f s (<= 1.2 / 3.0), back with "
			"the player %d/%d, relocations %d (0) | worst: %s",
			PolicyTag(), n, Pct(esc, 0.95), Pct(esc, 1.0), stuck.size(), Pct(stuck, 0.95), Pct(stuck, 1.0), back, n, relocs, worst.c_str());
	}

	// ---- the stuck ladder (ARCH 4.10; LEAD 10-14: never latched in a wall > 3 s; s9c fixer item 4: a leg pressed into a slope 8 s)
	inline void Stuck()
	{
		if (!Want("STUCK")) return;
		struct One
		{
			const char*            name;
			std::function<void(Scene&)> build;
		};
		const One set[] = {
			{ "stuck-table",
			  [](Scene& s) {
				  Level& lv = s.lv;
				  lv.interior = true;
				  lv.Floor(32, -96, 2368, 96, 0, "corridor");
				  lv.w.Box({ -8, -136, 0 }, { 2408, -120, 320 }, 0, "south");
				  lv.w.Box({ -8, 120, 0 }, { 2408, 136, 320 }, 0, "north");
				  lv.w.Box({ -8, -136, 0 }, { 8, 136, 320 }, 0, "west");
				  lv.w.Box({ 2392, -136, 0 }, { 2408, 136, 320 }, 0, "east");
				  lv.w.Box({ -8, -136, 320 }, { 2408, 136, 336 }, 2, "ceiling");
				  lv.w.Box({ 1100, -128, 0 }, { 1200, 128, 90 }, 0, "table");
				  lv.BuildNav();
				  s.start = { 200, 0, 0 };
				  s.playerFeet = { 2200, 0, 0 };
			  } },
			{ "stuck-poles",
			  [](Scene& s) {
				  Level& lv = s.lv;
				  lv.interior = false;
				  lv.Floor(-800, -1200, 3200, 1200, 0, "ground");
				  Rng rng{ 0x5107ull };
				  for (int i = 0; i < 70; ++i) lv.w.Pole({ rng.R(900, 1600), rng.R(-500, 500), 0 }, 6, 400, true);
				  lv.BuildNav();
				  s.start = { 300, 0, 0 };
				  s.playerFeet = { 2400, 0, 0 };
			  } },
			{ "stuck-pocket",
			  [](Scene& s) {
				  Level& lv = s.lv;
				  lv.interior = true;
				  lv.Floor(32, -768, 2976, 768, 0, "hall");
				  Box2(lv, 0, -800, 3008, 800, 360, "hall walls");
				  lv.w.Box({ -8, -808, 360 }, { 3016, 808, 376 }, 2, "ceiling");
				  lv.Obstacle({ 1300, -300, 0 }, { 1700, -284, 360 }, "pocket s");
				  lv.Obstacle({ 1300, 284, 0 }, { 1700, 300, 360 }, "pocket n");
				  lv.Obstacle({ 1700, -300, 0 }, { 1716, 300, 360 }, "pocket back");
				  lv.BuildNav();
				  s.start = { 300, 0, 0 };
				  s.playerFeet = { 2500, 0, 0 };
			  } },
			{ "stuck-slope",
			  [](Scene& s) {
				  Level& lv = s.lv;
				  lv.interior = false;
				  lv.Floor(-800, -2000, 4800, 2000, 0, "ground");
				  // a rock face leaning toward him (a 40 deg slope 700 u high) across the direct line, with a way round its ends
				  lv.w.Obb({ 1500, 0, 300 }, { 60, 900, 380 }, 0.0, 40.0, 0, "slope");
				  lv.Hole(1300, -940, 1700, 940, "slope cut");
				  lv.BuildNav();
				  s.start = { 300, 0, 0 };
				  s.playerFeet = { 3400, 0, 0 };
			  } },
		};
		std::vector<double> all;
		int                 relocs = 0, notBack = 0;
		std::string         lines;
		for (const One& o : set) {
			for (int seed = 1; seed <= Seeds(3, 1); ++seed) {
				Scene s;
				s.name = o.name;
				s.seed = static_cast<std::uint32_t>(seed);
				o.build(s);
				s.start = s.start + Vec3{ 0, (seed - 2) * 40.0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.reachPlayerR = 300;
				s.tEnd = 8.0;
				Res r = Run(s, C(60));
				for (double x : r.stuck) all.push_back(x);
				relocs += static_cast<int>(r.relocs.size());
				if (r.backWithPlayerAt < 0) ++notBack;
				if (seed == 1) {
					char b[300];
					std::snprintf(b, sizeof b, "%s: back %.2f s, stuck n %zu max %.2f, held %.2f s, relocs %zu; ", o.name, r.backWithPlayerAt, r.stuck.size(), Pct(r.stuck, 1.0), r.heldAtWall,
						r.relocs.size());
					lines += b;
				}
			}
		}
		CheckRoute("STUCK", Pct(all, 0.95) <= 1.2 && Pct(all, 1.0) <= 3.0 && relocs == 0 && notBack == 0,
			"[%s] a table across a corridor, a pole forest (rays miss), a pocket on the line, a slope leaning over the line: stuck episodes %zu p95 %.2f / max %.2f s (<= 1.2 / 3.0), relocations %d "
			"(0: never a put-back), not back with the player %d (0) | %s",
			PolicyTag(), all.size(), Pct(all, 0.95), Pct(all, 1.0), relocs, notBack, lines.c_str());
	}

	// ---- SIM-25 / sc13 replica / U63 / G5 / U07 / U10 / U52 / U61: the 20-minute teleport soak (no cage)
	inline Scene Soak(std::uint32_t seed, double minutes)
	{
		Scene s;
		s.name = "sim25";
		s.seed = seed;
		Level& lv = s.lv;
		lv.interior = false;
		// area A (space 1): open ground, a 600 u wall, crates, the roofed ruin
		lv.Floor(-3000, -4000, 9000, 4000, 0, "A ground");
		lv.Obstacle({ 3000, -1500, 0 }, { 3060, 1500, 600 }, "A wall600");
		lv.Obstacle({ 1500, 700, 0 }, { 1620, 820, 120 }, "A crate1");
		lv.Obstacle({ 1900, -900, 0 }, { 2000, -760, 140 }, "A crate2");
		Ruin(6000, 2000, lv);
		// area B (space 2): three rooms in a row (interior), doorways between
		const double bx = 40000;
		for (int i = 0; i < 3; ++i) lv.Floor(bx + i * 1024 + 32, -448 + 32, bx + (i + 1) * 1024 - 32, 448 - 32, 0, "B room");
		lv.w.Box({ bx - 8, -456, 0 }, { bx + 3080, -440, 320 }, 0, "B south");
		lv.w.Box({ bx - 8, 440, 0 }, { bx + 3080, 456, 320 }, 0, "B north");
		lv.w.Box({ bx - 8, -456, 0 }, { bx + 8, 456, 320 }, 0, "B west");
		lv.w.Box({ bx + 3064, -456, 0 }, { bx + 3080, 456, 320 }, 0, "B east");
		lv.w.Box({ bx - 8, -456, 320 }, { bx + 3080, 456, 336 }, 2, "B ceiling");
		lv.Wall(bx + 1024, -448, bx + 1024, 448, 0, 320, { { 128, 256 } }, 16, 208, "B w1");
		lv.Wall(bx + 2048, -448, bx + 2048, 448, 0, 320, { { -256, -128 } }, 16, 208, "B w2");
		lv.AddDoor(0x0D10B001u, { bx + 1024, 192, 0 }, true, 128, kOpen, "B d1");
		lv.AddDoor(0x0D10B002u, { bx + 2048, -192, 0 }, true, 128, kOpen, "B d2");
		// area C (space 3): a field with a house (an open door)
		const double cx = 80000;
		lv.Floor(cx - 3000, -3000, cx + 3000, 3000, 0, "C ground");
		lv.Obstacle({ cx + 1400, -500, 0 }, { cx + 1460, 500, 300 }, "C wall");
		lv.BuildNav();
		s.start = { 200, 120, 0 };
		s.playerFeet = { 0, 0, 0 };
		s.playerSpeed = 300;
		// the tour (seconds of the 20 min; scaled by minutes / 20)
		const double k = minutes / 20.0;
		auto         T_ = [k](double sec) { return sec * k; };
		s.playerPath = { { 800, 0, 0 }, { 800, 900, 0 }, { -600, 900, 0 }, { -600, -900, 0 }, { 1000, -900, 0 }, { 1000, 0, 0 }, { 2000, 0, 0 }, { 2600, -1800, 0 },
			             { 4200, -1800, 0 }, { 5000, 1000, 0 }, { 6700, 2000, 0 }, { 6000, 2000, 0 }, { 6800, 1300, 0 }, { 5000, 0, 0 } };
		s.playerStops = { { T_(60), T_(90) }, { T_(120), T_(170) }, { T_(200), T_(260) }, { T_(330), T_(380) } };
		// fight 1 (A, open): a melee raider + a shooter
		{
			Foe m = Shooter(0x00F25001u, { 2200, 1400, 0 }, 3.0, 0.8, T_(120));
			m.meleeV = 350;
			s.foes.push_back(m);
			s.foes.push_back(Shooter(0x00F25002u, { 2400, -1300, 0 }, 1.5, 0.6, T_(120)));
		}
		// fight 2 (A): a shooter behind the 600 u wall
		s.foes.push_back(Shooter(0x00F25003u, { 3500, 300, 0 }, 1.5, 0.6, T_(205)));
		// fight 3 (A): a raider inside the ruin
		s.foes.push_back(Shooter(0x00F25004u, { 5900, 2150, 0 }, 1.5, 0.6, T_(335)));
		// the load door into B at 7 min (space 2), the walk through the rooms, fight 4 in room 3 at 10 min
		{
			PlayerEv e;
			e.t = T_(420);
			e.feet = { bx + 300, -250, 0 };
			e.space = 2;
			e.path = { { bx + 900, -250, 0 }, { bx + 1100, 192, 0 }, { bx + 1700, 192, 0 }, { bx + 1900, -192, 0 }, { bx + 2300, -192, 0 }, { bx + 2700, 250, 0 }, { bx + 300, 250, 0 } };
			e.what = "loadDoor";
			s.events.push_back(e);
			s.foes.push_back(Shooter(0x00F25005u, { bx + 2850, 300, 0 }, 1.5, 0.6, T_(600), 2));
			Foe m = Shooter(0x00F25006u, { bx + 2850, -300, 0 }, 3.0, 0.8, T_(600), 2);
			m.meleeV = 350;
			s.foes.push_back(m);
		}
		// the fast travel to C at 13 min (space 3), fight 5 (3 raiders in the open), fight 6 behind the wall at 15 min
		{
			PlayerEv e;
			e.t = T_(780);
			e.feet = { cx, 0, 0 };
			e.space = 3;
			e.path = { { cx + 400, 300, 0 }, { cx + 900, -600, 0 }, { cx - 900, -600, 0 }, { cx - 900, 900, 0 }, { cx + 600, 900, 0 }, { cx, 0, 0 } };
			e.what = "fastTravel";
			s.events.push_back(e);
			s.foes.push_back(Shooter(0x00F25007u, { cx + 1200, 900, 0 }, 1.5, 0.6, T_(790), 3));
			s.foes.push_back(Shooter(0x00F25008u, { cx - 1100, -800, 0 }, 1.5, 0.6, T_(790), 3));
			Foe m = Shooter(0x00F25009u, { cx + 900, -1200, 0 }, 3.0, 0.8, T_(790), 3);
			m.meleeV = 350;
			s.foes.push_back(m);
			s.foes.push_back(Shooter(0x00F2500Au, { cx + 1900, 200, 0 }, 1.5, 0.6, T_(900), 3));
		}
		s.look = { { 0, Look::kAlong, {} }, { T_(120), Look::kAtPoint, { 2300, 0, 100 } }, { T_(180), Look::kAlong, {} }, { T_(470), Look::kDir, { -1, 0, 0 } },
			       { T_(480), Look::kAlong, {} }, { T_(782), Look::kDir, { -1, 0.3, 0 } }, { T_(800), Look::kAlong, {} } };
		s.tEnd = minutes * 60.0;
		return s;
	}
	inline void SIM25()
	{
		if (!Want("SIM-25")) return;
		const double minutes = g_quick ? 20.0 : 20.0;
		int          runs = 0, bad = 0, lost = 0, other = 0, seen = 0, foesLeft = 0;
		double       iuf = 0, stuckP95 = 0, stuckMax = 0, perTen = 0;
		std::string  br;
		for (int seed = 1; seed <= Seeds(2, 1); ++seed) {
			Scene s = Soak(static_cast<std::uint32_t>(seed), minutes);
			const auto t0 = std::chrono::steady_clock::now();
			Res r = Run(s, C(60));
			const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			++runs;
			bad += r.bad;
			lost += r.lostTransfers;
			other += static_cast<int>(r.relocs.size()) - r.lostTransfers;
			seen += r.seenRelocs;
			foesLeft += r.foes - r.kills;
			iuf = std::max(iuf, r.iufMax);
			stuckP95 = std::max(stuckP95, Pct(r.stuck, 0.95));
			stuckMax = std::max(stuckMax, Pct(r.stuck, 1.0));
			// all relocations per 10 minutes, worst 10-minute bin
			int b0 = 0, b1 = 0;
			for (const auto& row : r.relocs) (row.e.t < 600.0 ? b0 : b1)++;
			perTen = std::max(perTen, static_cast<double>(std::max(b0, b1)));
			char b[200];
			std::snprintf(b, sizeof b, " (desktop %.0f s)", wall);
			br = Brief(r) + b;
		}
		Check("SIM-25", bad == 0 && other == 0 && seen == 0 && lost == 2 * runs && perTen <= 2 && iuf <= 2.0 && stuckP95 <= 1.2 && stuckMax <= 3.0 && foesLeft == 0,
			"[%s] the 20-min teleport soak (a follow tour through open ground, a 600 u wall, a roofed ruin, 3 interior rooms, a field; 6 fights in 4 places; one load door + one fast travel; no "
			"cage), %d run(s): relocations = lost transfers %d (2 per run, after the space changes only), others %d (0), seen %d (0), forbidden %d (0), worst 10-min bin %.0f (<= 2); idle under "
			"fire max %.2f s (<= 2); stuck p95 %.2f / max %.2f s (<= 1.2 / 3.0); foes left %d (0) | %s",
			PolicyTag(), runs, lost, other, seen, bad, perTen, iuf, stuckP95, stuckMax, foesLeft, br.c_str());
	}

	// ---- FPS-1: the step-10 scenes at 30 / 60 / 120 / 144 / 240 fps with +-20 % jitter and 2-3.4 pumps per frame
	inline void FPS1()
	{
		if (!Want("FPS-1")) return;
		struct M
		{
			double jump{ -1 }, lost{ -1 }, back{ -1 };
			int    bad{ 0 }, seen{ 0 }, relocs{ 0 };
		};
		auto one = [&](double fps, bool jit) {
			M m;
			Cfg c = jit ? Jit(fps) : C(fps);
			{
				Res r = Run(Cage18(1, 0.40, 1, false, "fps-sc18"), c);
				m.jump = r.firstDefendAt >= 0 && r.firstShieldAt >= 0 ? r.firstDefendAt - r.firstShieldAt : -1;  // after the shield started
				m.bad += r.bad;
			}
			{
				Scene s = Fields(1, 0, 1, "fps-lost");
				s.tEnd = 12.0;
				Res r = Run(s, jit ? Jit(fps) : C(fps));
				m.lost = r.firstLostAt >= 0 ? r.firstLostAt - 1.0 : -1;
				m.bad += r.bad;
				m.seen += r.seenRelocs;
				m.relocs += static_cast<int>(r.relocs.size());
			}
			{
				Scene s;
				s.name = "fps-ruin";
				Level& lv = s.lv;
				lv.interior = false;
				lv.Floor(-1500, -2000, 4500, 2000, 0, "ground");
				Ruin(3000, 0, lv);
				lv.BuildNav();
				s.start = { 2800, -100, 0 };
				s.playerFeet = { 0, 0, 0 };
				s.look = { { 0, Look::kDir, { 1, 0, 0 } } };
				s.tEnd = 6.0;
				Res r = Run(s, jit ? Jit(fps) : C(fps));
				m.back = r.backWithPlayerAt;
				m.relocs += static_cast<int>(r.relocs.size());
				m.bad += r.bad;
			}
			return m;
		};
		const M     ref = one(60, false);
		int         bad = 0, badRoute = 0;
		std::string out;
		for (double fps : { 30.0, 60.0, 120.0, 144.0, 240.0 }) {
			const M m = one(fps, true);
			// every pass mark held (jump after the 1.0 s shield, in view at 40 %; lost 3.0..3.3 s, unseen; back <= 3 s; 0 forbidden) and
			// agreement with 60 fps within max(3 %, 0.1 s) (+ one 30 fps frame for the shot / decision quantisation)
			auto near = [&](double a, double b) { return a >= 0 && b >= 0 && std::abs(a - b) <= std::max(0.03 * b, 0.1) + 1.0 / 30.0; };
			const bool ok = m.bad == 0 && m.seen == 0 && m.relocs == 1 && m.jump >= 1.0 - 0.034 && m.lost > 3.0 - 0.034 && m.lost <= 3.3 && near(m.lost, ref.lost) && std::abs(m.jump - ref.jump) <= 0.6;
			if (!ok) ++bad;
			const bool okR = m.back > 0 && m.back <= 3.0 && near(m.back, ref.back);  // (the Route-1 travel's fps dependence)
			if (!okR) ++badRoute;
			char b[260];
			std::snprintf(b, sizeof b, "@%.0f: jump %.2f lost %.2f back %.2f bad %d seen %d relocs %d%s%s; ", fps, m.jump, m.lost, m.back, m.bad, m.seen, m.relocs, ok ? "" : " FAIL",
				okR ? "" : " back-off");
			out += b;
		}
		CheckRoute("FPS-1.route", badRoute == 0, "[%s] the ruin return (Route-1 travel) at 30-240 fps with jitter vs 60 fps (%.2f s): within max(3 %%, 0.1 s) + 1 frame and <= 3.0 s | %s", PolicyTag(),
			ref.back, out.c_str());
		Check("FPS-1", bad == 0,
			"[%s] sc18 at 40 %% looking (the jump >= 1.0 s after the shield started; shot timing differs by fps, so +-0.6 s) and the fast-travel lost transfer (3.0..3.3 s, unseen) at 30 / 60 / 120 / 144 / 240 fps with "
			"jitter and 2-3.4 pumps vs 60 fps (jump %.2f, lost %.2f, back %.2f): %s",
			PolicyTag(), ref.jump, ref.lost, ref.back, out.c_str());
	}

	// ---- the pooled teleport invariants (every closed-loop run above)
	inline void Pooled()
	{
		if (g_only && !Want("INV")) return;
		const Pool& p = P();
		Check("INV-11", p.bad == 0 && p.unexplained == 0 && p.seenNotLow == 0,
			"[%s] %d runs, %d frames: relocations %d (defend %d, lost %d, wedged %d); forbidden by the ground-truth oracle %d (0), unexplained torso jumps (a put-back / blink nobody judged) %d (0), "
			"seen relocations other than defend jumps below 50 %% %d (0) | first: %s",
			PolicyTag(), p.runs, p.frames, p.relocs, p.defend, p.lost, p.wedged, p.bad, p.unexplained, p.seenNotLow, p.firstBad.empty() ? "-" : p.firstBad.c_str());
		Check("INV-2", p.unshMax <= 0.25, "[%s] the shield case on ground truth (band-D foes hurting the player, none reachable, no G / C reachable): longest stretch without the shield or a jump "
		                                  "after the first second %.2f s (<= 0.25) in %s",
			PolicyTag(), p.unshMax, p.unshScene.empty() ? "-" : p.unshScene.c_str());
		Check("IUF", p.iufMax <= 2.0, "[%s] idle under fire (LEAD 10-16: 0 stretches > 2 s): longest %.2f s in %s; still while the player is hit (s9c item 1) longest %.2f s", PolicyTag(), p.iufMax,
			p.iufScene.empty() ? "-" : p.iufScene.c_str(), p.stillMax);
		CheckRoute("STUCK-pooled", Pct(p.stuck, 0.95) <= 1.2 && Pct(p.stuck, 1.0) <= 3.0,
			"[%s] stuck episodes (ARCH 4.10) over every run: %zu, p95 %.2f / max %.2f s (<= 1.2 / 3.0; LEAD 10-14 never latched > 3 s), worst in %s", PolicyTag(), p.stuck.size(),
			Pct(p.stuck, 0.95), Pct(p.stuck, 1.0), p.stuckWorst.c_str());
		Info("INV-13", "torso > 5 u inside a solid: %d frames over %d runs (step 8 / 9 own it; reported)", p.torsoIn, p.runs);
		Check("FINITE", p.nonFinite == 0, "%d runs with a NaN (0)", p.nonFinite);
		Info("COST", "the teleport layer per brain frame: p50 %.2f / p99 %.2f / max %.1f us (desktop)", Pct(p.costUs, 0.5), Pct(p.costUs, 0.99), Pct(p.costUs, 1.0));
	}

#include "s10_bcases.inc"

	inline void Run()
	{
		{
			auto t = Tele();
			T::PrintImpl(*t);
			auto st = Stack();
			A::PrintImpl(*st);
		}
		TP();
		SC18();
		MV15();
		Budget();
		Def0();
		Reach();
		Lost();
		Wedged();
		SIM18();
		MV11();
		Stuck();
		SIM25();
		FPS1();
		Pooled();
		BRun();
	}
}
