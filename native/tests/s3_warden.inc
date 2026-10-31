// OWNER: TESTS lane, round 6 step 3. WA-01..WA-32 (ARCH §7.3), the independent invariant checker (INV-1..INV-9) and the
// WA-10 fuzz (100 000 frames). Needs CHECK from the host and s3_api.h / s3_sim.h included before it (s3_all.inc).
// Unit cases feed synthetic facts straight into the Warden under test (the core's when it is in the tree); the closed-loop
// cases (WA-01, 04, 05, 11) run in s3_sim.h's world. Every case also prints one `S3CASE|<id>|PASS/FAIL|<detail>` line.
#include <cstdarg>
#include <functional>
#include <map>
#include <set>

namespace s3_tests
{
	namespace O = s3oracle;
	using sp::math::Vec3;
	using sp::math::Length;

	// ------------------------------------------------------------------------------------------------ case bookkeeping
	struct CaseRec
	{
		std::string id, detail;
		bool        pass{ true };
	};
	inline std::vector<CaseRec>& Cases()
	{
		static std::vector<CaseRec> v;
		return v;
	}
	struct CaseScope
	{
		std::string id;
		int         failAt;
		std::string detail;
		explicit CaseScope(const char* a_id) : id(a_id), failAt(g_fail) {}
		~CaseScope()
		{
			const bool ok = g_fail == failAt;
			std::printf("S3CASE|%s|%s|%s\n", id.c_str(), ok ? "PASS" : "FAIL", detail.c_str());
			Cases().push_back({ id, detail, ok });
		}
		void Note(const char* fmt, ...)
		{
			char    b[512];
			va_list a;
			va_start(a, fmt);
			std::vsnprintf(b, sizeof b, fmt, a);
			va_end(a);
			if (!detail.empty()) detail += "; ";
			detail += b;
		}
	};

	// ------------------------------------------------------------------------------------------------ the unit bench
	inline O::Cand Foe(std::uint32_t id, const Vec3& feet)
	{
		O::Cand c;
		c.id = id;
		c.feet = feet;
		c.chest = feet + Vec3{ 0, 0, 87 };
		c.height = 128;
		c.ev.hostile = true;
		c.ev.hostileKind = true;
		c.ev.boundR = 40;
		c.visible = true;
		return c;
	}
	inline void Shoots(O::Cand& c, double now)
	{
		c.ev.ctPlayer = true;
		c.ev.inCombat = true;
		c.ev.weaponDrawn = true;
		c.ev.ranged = true;
		c.ev.hitPlayerAt = now;
		c.ev.foughtGroupAt = now;
	}
	struct Bench
	{
		std::unique_ptr<s3api::IW> w;
		O::Params                  p{};
		O::WFacts                  f{};
		std::vector<O::Cand>       c;
		std::vector<O::ReachEst>   r;
		O::Decision                d{};
		double                     now{ 100.0 }, wall{ 5000.0 }, dt{ 1.0 / 60.0 };
		explicit Bench(std::unique_ptr<s3api::IW> a = s3api::MakeUnderTest()) : w(std::move(a))
		{
			f.player.feet = { 0, 0, 0 };
			f.player.eye = { 0, 0, 120 };
			f.player.health = 1.0;
			f.player.inCombat = true;
			Me({ 100, 0, 0 });
		}
		void Me(const Vec3& feet)
		{
			f.me.feet = feet;
			f.me.simTorso = feet + Vec3{ 0, 0, 90 };
			f.me.simFist = feet + Vec3{ 30, 0, 90 };
			f.me.eye = feet + Vec3{ 0, 0, 100 };
		}
		int Idx(std::uint32_t id) const
		{
			for (size_t i = 0; i < c.size(); ++i)
				if (c[i].id == id) return static_cast<int>(i);
			return -1;
		}
		O::Cand& C(std::uint32_t id) { return c[static_cast<size_t>(Idx(id))]; }
		O::ReachEst& R(std::uint32_t id) { return r[static_cast<size_t>(Idx(id))]; }
		O::Cand& Add(const O::Cand& a)
		{
			c.push_back(a);
			r.push_back({});
			Direct(a.id);
			return c.back();
		}
		void Remove(std::uint32_t id)
		{
			const int i = Idx(id);
			if (i < 0) return;
			c.erase(c.begin() + i);
			r.erase(r.begin() + i);
		}
		double DistMe(std::uint32_t id) { return Length(C(id).chest - f.me.simTorso); }
		void Direct(std::uint32_t id)
		{
			O::ReachEst& e = R(id);
			const double d = DistMe(id);
			e = {};
			e.kind = O::ReachKind::kDirect;
			e.eta = 0.05 + d / (f.me.interiorCell ? 2900.0 : 3200.0);
			e.len = d;
			e.confirmed = true;
			e.at = now;
		}
		void Route(std::uint32_t id, double len)
		{
			O::ReachEst& e = R(id);
			e = {};
			e.kind = O::ReachKind::kRoute;
			e.eta = 0.10 + len / 3200.0;
			e.len = len;
			e.confirmed = true;
			e.at = now;
		}
		void Eta(std::uint32_t id, double eta, bool confirmed = true)  // a direct estimate with a chosen ETA
		{
			O::ReachEst& e = R(id);
			e = {};
			e.kind = O::ReachKind::kDirect;
			e.eta = eta;
			e.len = DistMe(id);
			e.confirmed = confirmed;
			e.at = now;
		}
		void Jump(std::uint32_t id)
		{
			O::ReachEst& e = R(id);
			e = {};
			e.kind = O::ReachKind::kJumpOnly;
			e.eta = 0.35;
			e.len = DistMe(id);
			e.confirmed = true;
			e.at = now;
		}
		void Unknown(std::uint32_t id)
		{
			O::ReachEst& e = R(id);
			const double d = DistMe(id);
			e = {};
			e.kind = O::ReachKind::kUnknown;
			e.eta = 1.25 * d / 3200.0 + 0.2;
			e.len = d;
			e.confirmed = false;
			e.at = now;
		}
		const O::Decision& Step()
		{
			const Vec3 centre = f.gates.waiting ? f.gates.waitSpot : f.player.feet;
			for (auto& x : c) {
				x.dGuard = O::GuardDist(x.feet, centre);
				x.dMe = Length(x.chest - f.me.simTorso);
				if (x.visible) x.seenAt = now;
			}
			f.now = now;
			f.wall = wall;
			f.dt = dt;
			f.cands = c;
			f.reach = r;
			w->Update(f, p, d);
			now += dt;
			wall += dt;
			return d;
		}
		// run for a_s seconds, calling a_each before every frame and a_post after it (with this frame's Decision); returns the frames
		int Run(double a_s, const std::function<void()>& a_each = {}, const std::function<void()>& a_post = {})
		{
			const int n = static_cast<int>(std::lround(a_s / dt));
			for (int i = 0; i < n; ++i) {
				if (a_each) a_each();
				Step();
				if (a_post) a_post();
			}
			return n;
		}
		O::Score Sc(std::uint32_t id)
		{
			O::Score s;
			s.never = O::Never::kCount;
			w->ScoreOf(id, s);
			return s;
		}
	};

	// ------------------------------------------------------------------------------------------------ independent invariants
	// Ground truth for the fuzz and the closed loops: what the never-set table says on the generator's facts (§3.3),
	// evaluated here without the Warden's code; the turned-companion debounce is tracked per id by the checker.
	struct Inv
	{
		std::map<std::uint32_t, double> turnSince;      // raw turn test true since
		std::map<std::uint32_t, double> restUntil;      // essential rest windows (wall), from the knock-downs the generator made
		std::map<std::uint32_t, double> okSince;        // INV-4: same-band challenger satisfied the ratio since (vs the target)
		std::map<std::uint32_t, double> leftAt;         // pair-flip memory (INV-4 / INV-9)
		std::set<std::uint32_t>         unreachIds;     // step 4 integrator: ids the old movement failed to reach twice (warden.cnlUnreach 1:
		                                                // not reachable, so leaving one is a hand-over like a jump-only target; set by the s4 loop)
		// step 4 fix lane 1 (warden.cageKeep, ARCH 4.11): every reach failure of the old movement (brain clock, set by the s4 loop)
		// and the cage rule's numbers (0 = off). The checker computes the cage itself (seed: a failure within cageFor and not a
		// direct reach; caged: not direct and within cageR of a seed) - inside one cage the target is KEPT against a better
		// challenger (no INV-9) and a switch to the one hurting the player is a hand-over (no INV-4)
		std::map<std::uint32_t, double> failAtAll;
		double                          cageR{ 0 }, cageFor{ 0 };
		double                          hurtWin{ 0 };   // s4f (warden.hurtKeep): a target that hit the player within hurtWin s is kept against a non-hurting challenger (0 = off)
		std::set<std::uint32_t>         cagedNow;
		std::uint32_t prevTarget{ 0 };
		O::Band       prevBand{ O::Band::kNone };
		std::uint32_t inv9Id{ 0 };
		double        inv9Since{ -1 };
		double        recallUntil{ -1e9 };
		std::array<int, 10> viol{};
		std::vector<std::string> ring;  // the last frames (INV-4 trace on the first violation)
		std::array<std::string, 10> first{};
		void V(int k, const std::string& why)
		{
			if (viol[k]++ == 0) first[k] = why;
		}
		static bool CtGroup(const O::Evidence& e) { return e.ctPlayer || e.ctHim || e.ctMate; }
		bool TurnHeld(const O::Cand& c, const O::WFacts& f, const O::Params& p)
		{
			const O::Evidence& e = c.ev;
			const bool raw = e.hostile && !e.teammate && (e.ctPlayer || f.now - e.hitPlayerAt <= 3.0);
			auto it = turnSince.find(c.id);
			if (!raw) { turnSince.erase(c.id); return false; }
			if (it == turnSince.end()) { turnSince[c.id] = f.now; return p.turnDebounce <= 0; }
			return f.now - it->second >= p.turnDebounce - 1e-9;
		}
		// true = never-set by rows N01..N13 / N15 (N14 needs the band, N16 the latch)
		static bool TruthNever(const O::Cand& c, const O::WFacts& f, const O::Params& p, bool turned)
		{
			const O::Evidence& e = c.ev;
			if (c.id == 0 || c.id == O::kPlayerId || c.id == O::kHimId || e.self || e.child) return true;
			if (e.teammate) return true;
			if ((e.companion || e.dogmeat || c.id == O::kDogmeatId) && !turned) return true;
			if (e.commanded) return true;
			if (e.settlerNpc && p.settlerDefend == 0) return true;
			if (e.workshopLink && e.playerWorkshop && !e.hostile) return true;
			const bool hostileNow = e.hostile || CtGroup(e) || (e.hostileKind && f.now - e.playerHitItAt <= p.playerFightFor);
			if (!hostileNow) return true;
			if (e.down || e.killPending || e.speaker || e.ghost || !e.loaded || !e.sameSpace || e.mount || e.playerKillMoveOnIt) return true;
			return false;
		}
		static O::Band RawBand(const O::Cand& c, const O::WFacts& f, const O::Params& p, bool turned)
		{
			const O::Evidence& e = c.ev;
			const bool km = e.inKillMoveAttacker || e.inKillMoveVictim;
			if (km) return (e.hostile && e.inCombat) ? O::Band::kCombatant : O::Band::kNone;
			if (f.now - e.hitPlayerAt <= p.defendWindow || e.ctPlayer || ((e.companion || e.dogmeat) && turned)) return O::Band::kDefend;
			if (f.now - std::max(e.hitHimAt, e.hitMateAt) <= p.defendWindow || e.ctHim || e.ctMate) return O::Band::kGuard;
			return O::Band::kNone;  // C / F evidence is not needed by the checks below (they only bound from D / G)
		}
		static bool ReachableNonJump(const O::ReachEst& r, double now)
		{
			if (r.kind == O::ReachKind::kJumpOnly) return false;
			if (r.kind == O::ReachKind::kNone) return now >= r.noneUntil;
			return true;
		}
		static double RangeOf(O::Band b, const O::Cand& c, const O::WFacts& f, const O::Params& p)
		{
			return O::RangeOf(b, c.ev.inCombat, p.range, f.gates.waiting);
		}
		// one frame: the decision d of the Warden under test for the facts f
		// s4f (warden.hurtKeep): the candidate hit the player within a_win s
		static bool HurtsNow(const O::WFacts& a_f, std::uint32_t a_id, double a_win)
		{
			for (const auto& c : a_f.cands)
				if (c.id == a_id) return a_f.now - c.ev.hitPlayerAt <= a_win;
			return false;
		}
		void Check(const O::WFacts& f, const O::Params& p, const O::Decision& d, s3api::IW& w)
		{
			const bool gated = f.gates.paused || f.gates.sentAway || f.gates.carrying || f.gates.escorting || f.me.down;
			const Vec3 centre = f.gates.waiting ? f.gates.waitSpot : f.player.feet;
			std::map<std::uint32_t, bool> turned;
			for (const auto& c : f.cands) turned[c.id] = TurnHeld(c, f, p);
			const O::Cand* tc = nullptr;
			size_t ti = 0;
			for (size_t i = 0; i < f.cands.size(); ++i)
				if (f.cands[i].id == d.target) { tc = &f.cands[i]; ti = i; }
			// step 4 fix lane 1: the cage (computed here from the loop's failure records, not from the core)
			cagedNow.clear();
			if (cageFor > 0) {
				auto direct = [&](size_t i) { return i < f.reach.size() && f.reach[i].kind == O::ReachKind::kDirect; };
				std::vector<size_t> seeds;
				for (size_t i = 0; i < f.cands.size(); ++i) {
					auto it = failAtAll.find(f.cands[i].id);
					if (it != failAtAll.end() && f.now >= it->second && f.now - it->second <= cageFor + 1e-9 && !direct(i)) seeds.push_back(i);
				}
				for (size_t i = 0; i < f.cands.size() && !seeds.empty(); ++i) {
					if (direct(i)) continue;
					for (size_t s : seeds) {
						Vec3 dd = f.cands[s].feet - f.cands[i].feet;
						dd.z = 0;
						if (s == i || sp::math::Length(dd) <= cageR) {
							cagedNow.insert(f.cands[i].id);
							break;
						}
					}
				}
			}
			// INV-1
			if (d.target) {
				if (!tc) V(1, "target not in the candidate list");
				else if (TruthNever(*tc, f, p, turned[tc->id])) {
					char b[400];
					std::snprintf(b, sizeof b, "t=%.3f target %08X is never-set (ground truth)", f.now, d.target);
					V(1, b);
				} else if (tc->denied && !(d.band == O::Band::kDefend || d.band == O::Band::kGuard)) {
					V(1, "a denied actor picked outside bands D / G");
				}
			}
			// eligible-by-truth max raw band over reachable (non-jump, finite) foes inside their range
			O::Band maxRaw = O::Band::kNone;
			std::uint32_t maxId = 0;
			bool g2 = false;
			std::uint32_t g2Id = 0;
			O::Band g2Band = O::Band::kNone;
			O::ReachKind g2Reach = O::ReachKind::kUnknown;
			for (size_t i = 0; i < f.cands.size(); ++i) {
				const O::Cand& c = f.cands[i];
				if (TruthNever(c, f, p, turned[c.id])) continue;
				const O::Band b = RawBand(c, f, p, turned[c.id]);
				if (b < O::Band::kGuard) continue;
				if (c.denied && b < O::Band::kGuard) continue;
				if (c.ev.essentialOnly) continue;  // rests may skip it (INV-7 checks them)
				if (O::GuardDist(c.feet, centre) > RangeOf(b, c, f, p)) continue;
				const O::ReachEst re = i < f.reach.size() ? f.reach[i] : O::ReachEst{};
				// INV-2 (a): a REACHABLE (finite, not jump-only) eligible band >= G foe -> a target. ORACLE READING (TESTPLAN §6 Q1):
				// a jump-only foe counts as unreachable here, as in §3.9.5 / INV-2 (b) (the shield / defend-jump case)
				const bool finite = ReachableNonJump(re, f.now);
				if (finite && !g2) {
					g2 = true;
					g2Id = c.id;
					g2Band = b;
					g2Reach = re.kind;
				}
				if (!ReachableNonJump(re, f.now)) continue;
				if (b > maxRaw) { maxRaw = b; maxId = c.id; }
			}
			// INV-2
			if (!gated && g2 && !d.target) {
				char b[400];
				O::Score gs;
				const bool hs = w.ScoreOf(g2Id, gs);
				std::snprintf(b, sizeof b, "t=%.3f idle (intent %d why=%s) while %08X raw band %s reach %s (impl: %s never=%s band=%s eta=%.2f resting=%d) exists", f.now, int(d.intent), d.why, g2Id,
				              O::BandName(g2Band), O::ReachName(g2Reach), hs ? "scored" : "no score", O::NeverCode(gs.never), O::BandName(gs.band), gs.eta >= O::kInf ? -1.0 : gs.eta, int(gs.resting));
				V(2, b);
			}
			// INV-2 (b): band-D foes exist (eligible by truth), and NOTHING eligible is reachable (finite, not jump-only) -> the shield
			// permission (intent Shield, or the jump-only band-D target with the shield first: never Follow / Hang)
			if (!gated) {
				bool anyD = false, anyReach = false;
				std::uint32_t dId = 0;
				for (size_t i = 0; i < f.cands.size(); ++i) {
					const O::Cand& c = f.cands[i];
					if (TruthNever(c, f, p, turned[c.id]) || (c.denied && RawBand(c, f, p, turned[c.id]) < O::Band::kGuard)) continue;
					const O::ReachEst re = i < f.reach.size() ? f.reach[i] : O::ReachEst{};
					if (ReachableNonJump(re, f.now)) anyReach = true;
					if (!c.ev.essentialOnly && RawBand(c, f, p, turned[c.id]) == O::Band::kDefend && O::GuardDist(c.feet, centre) <= RangeOf(O::Band::kDefend, c, f, p)) {
						anyD = true;
						dId = c.id;
					}
				}
				if (anyD && !anyReach && !d.mayShield) {
					char b[400];
					std::snprintf(b, sizeof b, "t=%.3f band-D %08X exists, nothing reachable, but no shield (target %08X intent %d)", f.now, dId, d.target, int(d.intent));
					V(2, b);
				}
			}
			// INV-3 (bound from below: holds only raise bands)
			if (!gated && d.target && !d.finishWindow && maxRaw != O::Band::kNone && static_cast<int>(d.band) < static_cast<int>(maxRaw)) {
				char b[400];
				std::snprintf(b, sizeof b, "t=%.3f target %08X band %s below a reachable band-%s foe %08X", f.now, d.target, O::BandName(d.band),
				              O::BandName(maxRaw), maxId);
				V(3, b);
			}
			// INV-6
			if (d.target && d.s.eta < O::kInf) {
				const double u = d.s.lethal * d.s.ev * d.s.ess / (d.s.eta + d.s.tKill);
				if (std::abs(u - d.s.U) > 1e-9 * std::max(1.0, std::abs(d.s.U))) {
					char b[400];
					std::snprintf(b, sizeof b, "U %.12g vs breakdown %.12g", d.s.U, u);
					V(6, b);
				}
			}
			// INV-7
			if (d.target && tc && tc->ev.essentialOnly) {
				auto it = restUntil.find(d.target);
				if (it != restUntil.end() && f.wall < it->second - 1e-6) {
					for (size_t i = 0; i < f.cands.size(); ++i) {
						const O::Cand& c = f.cands[i];
						if (c.id == d.target || c.ev.essentialOnly || TruthNever(c, f, p, turned[c.id])) continue;
						const O::Band b = RawBand(c, f, p, turned[c.id]);
						if (b == O::Band::kNone || static_cast<int>(b) < static_cast<int>(d.band)) continue;
						if (O::GuardDist(c.feet, centre) > RangeOf(b, c, f, p)) continue;
						if (!ReachableNonJump(i < f.reach.size() ? f.reach[i] : O::ReachEst{}, f.now)) continue;
						char bb[160];
						std::snprintf(bb, sizeof bb, "t=%.3f resting essential %08X picked while %08X (band %s) is reachable", f.now, d.target, c.id, O::BandName(b));
						V(7, bb);
						break;
					}
				}
			}
			// INV-8
			if (d.target && tc) {
				const double R = RangeOf(d.band, *tc, f, p);
				if (O::GuardDist(tc->feet, centre) > R + p.rangeHyst + 1e-6) {
					char b[400];
					std::snprintf(b, sizeof b, "t=%.3f target %08X at %.0f u > R %.0f + hyst", f.now, d.target, O::GuardDist(tc->feet, centre), R);
					V(8, b);
				}
			}
			if (prevTarget && d.target != prevTarget) {
				for (const auto& c : f.cands)
					if (c.id == prevTarget) {
						O::Score s;
						if (w.ScoreOf(c.id, s) && s.never == O::Never::kRange && s.band != O::Band::kNone && O::GuardDist(c.feet, centre) <= RangeOf(s.band, c, f, p) - 1e-6) {
							char b[400];
							std::snprintf(b, sizeof b, "t=%.3f range drop of %08X inside R", f.now, c.id);
							V(8, b);
						}
					}
			}
			// INV-4: a same-band switch only after a confirmed ratio held >= the fast hold (or urgent)
			if (prevTarget && d.target && d.target != prevTarget) {
				O::Score sx, sy;
				const bool hx = w.ScoreOf(prevTarget, sx), hy = w.ScoreOf(d.target, sy);
				const bool jumpHandOver = (sx.reach == O::ReachKind::kJumpOnly && sy.reach != O::ReachKind::kJumpOnly) ||  // TESTPLAN §6 Q2
				                          (unreachIds.count(prevTarget) != 0 && unreachIds.count(d.target) == 0) ||       // step 4 integrator (cnlUnreach)
				                          (cagedNow.count(prevTarget) != 0 && cagedNow.count(d.target) != 0);             // step 4 fix lane 1 (cage: why=hurting)
				if (hx && hy && sx.never == O::Never::kOk && !sx.resting && !jumpHandOver && sx.eta < O::kInf && sx.band == sy.band) {
					const bool urgent = sy.confirmed && sy.U >= p.urgentRatio * sx.U - 1e-12;
					auto it = okSince.find(d.target);
					const bool held = it != okSince.end() && it->second >= 0 && f.now - it->second >= p.switchHoldFast - f.dt - 1e-6;
					if (!urgent && !held) {
						char b[400];
						std::snprintf(b, sizeof b, "t=%.3f same-band switch %08X -> %08X why=%s without a held ratio (U %.3f vs %.3f; reach %s/%s conf %d/%d band %s)", f.now, prevTarget, d.target, d.why, sy.U, sx.U,
						              O::ReachName(sy.reach), O::ReachName(sx.reach), int(sy.confirmed), int(sx.confirmed), O::BandName(sy.band));
						if (viol[4] == 0) {
							std::printf("INV-4 trace (impl %s):\n", w.Name());
							for (const auto& l : ring) std::printf("  %s\n", l.c_str());
						}
						V(4, b);
					}
				}
			}
			// INV-4 bookkeeping for the next frame (vs the current target)
			{
				char rb[300];
				O::Score s4;
				const bool h4 = d.second && w.ScoreOf(d.second, s4);
				std::snprintf(rb, sizeof rb, "t=%.4f dt=%.4f tgt=%08X b=%s U=%.3f why=%s hidden=%d | 2nd=%08X b=%s U=%.3f conf=%d okSince=%.4f", f.now, f.dt, d.target, O::BandName(d.band), d.s.U, d.why,
				              tc ? int(f.now - tc->seenAt > 0.5) : -1, d.second, h4 ? O::BandName(s4.band) : "?", h4 ? s4.U : -1.0, h4 ? int(s4.confirmed) : -1,
				              okSince.count(d.second) ? okSince[d.second] : -2.0);
				ring.push_back(rb);
				if (ring.size() > 14) ring.erase(ring.begin());
				O::Score st;
				const bool ht = d.target && w.ScoreOf(d.target, st);
				for (const auto& c : f.cands) {
					if (c.id == d.target) continue;
					O::Score sc;
					const bool ok = ht && w.ScoreOf(c.id, sc) && sc.never == O::Never::kOk && sc.band == st.band && sc.confirmed &&
					                sc.U >= p.switchRatio * st.U - 1e-12;
					if (!ok) okSince[c.id] = -1;
					else if (okSince[c.id] < 0) okSince[c.id] = f.now;
				}
			}
			// INV-9: a hidden target (> 0.5 s) loses to a confirmed same-band challenger with U >= 1.35 x within 0.15 s
			if (d.target && tc && !gated && f.now - tc->seenAt > 0.5 && d.second && d.s2.band == d.band && d.s2.confirmed &&
			    !(cagedNow.count(d.target) != 0 && cagedNow.count(d.second) != 0) &&  // step 4 fix lane 1: kept inside one cage (ARCH 4.11)
			    !(hurtWin > 0 && HurtsNow(f, d.target, hurtWin) && !HurtsNow(f, d.second, hurtWin)) &&  // s4f: the hurt keep (the core INV-9 checks its urgent limit)
			    d.s2.reach != O::ReachKind::kJumpOnly && d.s.reach != O::ReachKind::kJumpOnly && d.s2.U >= p.switchRatio * d.s.U &&
			    (leftAt.find(d.second) == leftAt.end() || f.now - leftAt[d.second] >= p.pairWindow)) {
				if (inv9Id != d.second) { inv9Id = d.second; inv9Since = f.now; }
				else if (f.now - inv9Since > p.switchHoldFast + 2 * f.dt + 1e-6) {
					char b[400];
					std::snprintf(b, sizeof b, "t=%.3f hidden target %08X kept %.2f s against %08X (U %.3f vs %.3f)", f.now, d.target, f.now - inv9Since, d.second, d.s2.U, d.s.U);
					V(9, b);
					inv9Since = f.now;  // count once per 0.15 s
				}
			} else {
				inv9Id = 0;
			}
			if (prevTarget && d.target != prevTarget) leftAt[prevTarget] = f.now;
			prevTarget = d.target;
			prevBand = d.band;
			(void)ti;
		}
		int Total() const
		{
			int n = 0;
			for (int v : viol) n += v;
			return n;
		}
	};
}
