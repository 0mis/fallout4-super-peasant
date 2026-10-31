// OWNER: B1 BRAIN-CORE (brain2).  The native Super Peasant brain (see brain.h). Pure C++: no engine calls; geometry
// only through IWorld. Rule origins: np_brain.nut (L4D2) Update / Pick / ClearTo / Approach / OverTheTop / Unreachable /
// BlinkTo, ROUND2_PLAN §3-§4 (tiers, far tier, vetting, caps), HUNT_SPEC §2-§4 (far order, cruise law, planner).
#include "core/brain.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string_view>

namespace sp::brain
{
	using sp::math::Dot;
	using sp::math::Length;
	using sp::math::LengthSq;
	using sp::math::Normalize;

	namespace
	{
		constexpr double kTiny = 1e-9;
		// s12 fix round 3 lane 2 (brain.essDownHold): a hit on the player this long after the noted knock-down is a NEW shot (it is
		// up); the knock-down is noted punch.nativeDamageDelay (0.3 s) after the punch, and a shot already flying lands within that
		// (hf 130907: the fling at 10.871, its shot drained at 10.958, the knock-down noted at 11.136)
		constexpr double kEssDownUpHit = 0.25;

		Vec3 RotateZ(const Vec3& v, double a_deg)
		{
			const double a = a_deg * sp::math::kDeg2Rad, c = std::cos(a), s = std::sin(a);
			return { v.x * c - v.y * s, v.x * s + v.y * c, v.z };
		}

		int FarRank(Tier t)
		{
			switch (t) {
			case Tier::kFar1: return 4;
			case Tier::kFar2: return 3;
			case Tier::kFar3: return 2;
			case Tier::kFarP: return 1;  // ROUND3: far first strike ranks below every fighting far foe
			default:          return 0;
			}
		}

		Vec3 HeadOf(const Cand& c)
		{
			const Vec3 zero{};
			return c.head == zero ? c.feet + Vec3{ 0, 0, c.height } : c.head;
		}

		// s11 fix lane 2 (brain.farHuntPlayerR): a classified foe aiming at the player within farHuntPlayerR of it (a_in: Brain::Info)
		template <class I>
		bool NearPlayerFoe(const Params& p, const Cand& c, const I& a_in)
		{
			return p.farHuntPlayerR > 0 && (c.attackingPlayer || c.combatWithPlayer) && c.hostile && !c.killPending && !c.essentialOnly &&
			       a_in.tier != Tier::kNone && a_in.dPlayer <= p.farHuntPlayerR + kTiny;
		}
		// s12 fix lane 3 (brain.farHuntGroup; s12 gate soak 225511 dt3 23:06:25.49: right after the kill of the burst raider
		// FF0017F7 he picked the street raider 0013BE5F 5201 u away (tier F2, its combat target the player) while the burst raider
		// FF0017EB - fighting HIM, so no NearPlayerFoe - was still up near the player; the round-5-C defend-first rule (farFirst)
		// skips every local foe that does not aim at the player): a classified foe FIGHTING THE GROUP (combat target the player /
		// him / a teammate, or a hit on the player / him / a teammate within hitMemory) within farHuntPlayerR of the player
		template <class I>
		bool NearGroupFoe(const Params& p, const Cand& c, const I& a_in, double a_now)
		{
			if (!(p.farHuntGroup >= 0.5 && p.farHuntPlayerR > 0) || !c.hostile || c.killPending || c.essentialOnly || a_in.tier == Tier::kNone) return false;
			const double hitAny = a_now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt });
			return (c.combatWithGroup || hitAny <= p.hitMemory + kTiny) && a_in.dPlayer <= p.farHuntPlayerR + kTiny;
		}
	}

	// ============================================================================================ names
	const char* TierName(Tier t)
	{
		switch (t) {
		case Tier::kPreempt:  return "P";
		case Tier::kEngaged:  return "E";
		case Tier::kAttacker: return "A";
		case Tier::kFar3:     return "F3";
		case Tier::kFar2:     return "F2";
		case Tier::kFar1:     return "F1";
		case Tier::kFarP:     return "FP";
		default:              return "-";
		}
	}
	const char* ClsName(Cls c) { return c == Cls::kRanged ? "R" : (c == Cls::kBig ? "B" : "r"); }
	const char* MoveName(Move m)
	{
		switch (m) {
		case Move::kApproach: return "A";
		case Move::kHunt:     return "H";
		case Move::kOverTop:  return "O";
		case Move::kAiChase:  return "C";
		case Move::kReturn:   return "R";
		case Move::kHold:     return "X";
		case Move::kNav:      return "N";
		default:              return "F";
		}
	}
	const char* GiveUpName(GiveUp g)
	{
		switch (g) {
		case GiveUp::kBlind:         return "blind";
		case GiveUp::kUnreach:       return "unreach";
		case GiveUp::kHunt:          return "hunt";
		case GiveUp::kLeash:         return "leash";
		case GiveUp::kInteriorBlind: return "interior-blind";
		case GiveUp::kNoPath:        return "nopath";
		case GiveUp::kChain:         return "chain";
		case GiveUp::kRecall:        return "recall";
		default:                     return "none";
		}
	}

	// ============================================================================================ params
	Params Params::Load()
	{
		Params p;
		p.indoorMaxSpeed = sp::cfg::Tunable("hotfix.indoorMaxSpeed", p.indoorMaxSpeed);  // the HOTFIX cap is the default
#define SP_BRAIN_LOAD(n, d) p.n = sp::cfg::Tunable("brain." #n, p.n);
		SP_BRAIN_PARAMS(SP_BRAIN_LOAD)
#undef SP_BRAIN_LOAD
		return p;
	}

	double Params::FarR(int a_uGrids) const
	{
		const int    g = std::max(1, a_uGrids);
		const double guaranteed = static_cast<double>((g - 1) / 2) * cellSize;
		return std::max(0.0, std::min(farR, std::max(guaranteed, cellSize * 0.5)));
	}

	double Params::MaxR(int a_uGrids) const
	{
		const int    g = std::max(1, a_uGrids);
		const double loaded = static_cast<double>((g + 1) / 2) * cellSize;
		return std::max(FarR(a_uGrids), std::min(maxR, loaded));
	}

	double Params::HuntAbort(int a_uGrids) const { return std::min(MaxR(a_uGrids) + huntAbortPad, huntAbortMax); }

	int Params::Check(int a_uGrids, std::string* a_why) const
	{
		int  n = 0;
		auto bad = [&](bool a_ok, const char* a_text) {
			if (a_ok) return;
			++n;
			if (a_why) {
				a_why->append(a_text);
				a_why->push_back('\n');
			}
		};
		const double mr = MaxR(a_uGrids), fr = FarR(a_uGrids), ab = HuntAbort(a_uGrids);
		bad(mr + leashExtra < ab, "maxR + leashExtra < huntAbort");
		bad(ab < papHuntLost, "huntAbort < HUNT_LOST");
		bad(fr <= mr, "farR <= maxR");
		bad(guardStill < papCatchup, "guardStill < CATCHUP_DIST");
		bad(papCatchup < guardLost, "CATCHUP_DIST < guard.lost");
		bad(huntChainMax < papHuntBackstop, "huntChainMax < HUNT_BACKSTOP");
		bad(huntChainMax + huntRest < 2.0 * papSeparation, "huntChainMax + huntRest < 2 x SEPARATION_MAX");
		bad(attackerGuard + leashExtra < papHuntLost, "attackerGuard + leashExtra < HUNT_LOST");
		bad(sightMax >= mr, "sightMax >= maxR");
		bad(handR > approachMin && swingRange >= handR, "approachMin < handR <= swingRange");
		bad(rangedGuard <= preemptFar && preemptFar <= preemptFarBusy, "rangedGuard <= preemptFar <= preemptFarBusy");      // ROUND3
		bad(preemptAnywhere <= preemptAnywhereBusy, "preemptAnywhere <= preemptAnywhereBusy");
		return n;
	}

	int Params::Clamp(int a_uGrids)
	{
		int fixes = 0;
		auto fix = [&](double& a_v, double a_to) {
			if (a_v != a_to) {
				a_v = a_to;
				++fixes;
			}
		};
		if (huntAbortPad <= leashExtra) fix(huntAbortPad, leashExtra + 1);
		if (huntAbortMax >= papHuntLost) fix(huntAbortMax, papHuntLost - 1);
		if (MaxR(a_uGrids) + leashExtra >= HuntAbort(a_uGrids)) fix(maxR, huntAbortMax - leashExtra - 1);
		if (farR > maxR) fix(farR, maxR);
		if (guardStill >= papCatchup) fix(guardStill, papCatchup - 1);
		if (huntChainMax >= papHuntBackstop) fix(huntChainMax, papHuntBackstop - 1);
		if (huntChainMax + huntRest >= 2.0 * papSeparation) fix(huntChainMax, std::max(1.0, 2.0 * papSeparation - huntRest - 1));
		if (attackerGuard + leashExtra >= papHuntLost) fix(attackerGuard, papHuntLost - leashExtra - 1);
		if (sightMax < MaxR(a_uGrids)) fix(sightMax, MaxR(a_uGrids));
		if (handR <= approachMin) fix(handR, approachMin + 1);
		if (swingRange < handR) fix(swingRange, handR);
		if (preemptFar < rangedGuard) fix(preemptFar, rangedGuard);  // ROUND3: clamp up
		if (preemptFarBusy < preemptFar) fix(preemptFarBusy, preemptFar);
		if (preemptAnywhereBusy < preemptAnywhere) fix(preemptAnywhereBusy, preemptAnywhere);
		return fixes;
	}

	// ============================================================================================ pure helpers
	double GuardDist(const Vec3& a, const Vec3& b)
	{
		const double dx = a.x - b.x, dy = a.y - b.y, dz = (a.z - b.z) * 0.5;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	double Vert(double a_dz, double a_hoverStep) { return (a_dz > a_hoverStep || a_dz < -a_hoverStep) ? a_dz : 0.0; }
	Vec3   Flat(const Vec3& v) { return { v.x, v.y, 0 }; }
	Vec3   FlatDir(const Vec3& v)
	{
		const double l = std::sqrt(v.x * v.x + v.y * v.y);
		return l > 1e-3 ? Vec3{ v.x / l, v.y / l, 0 } : Vec3{};
	}

	double SightStep(double a_sight, bool a_hasTarget, bool a_targetVisible, double a_dt)
	{
		return std::clamp(a_sight + ((!a_hasTarget || a_targetVisible) ? a_dt : -a_dt), 0.0, 1.0);
	}

	double Priority(Cls a_cls, bool a_pin, double a_dPlayer, bool a_occluded, const Params& p)
	{
		const double base = a_cls == Cls::kBig ? p.prioBig : (a_cls == Cls::kRanged ? p.prioRanged : p.prioRegular);
		const double g = a_dPlayer < p.threat ? 10.0 : a_dPlayer < p.near4 ? 4.0 : a_dPlayer < p.near8 ? 2.0 : a_dPlayer < p.near14 ? 1.25 : 1.0;
		return std::max(1e-6, base * (a_pin ? p.pinPrio : 1.0) * g * (a_occluded ? p.occludedPrio : 1.0));
	}

	double Score(const Vec3& a_chest, const Vec3& a_meChest, const Vec3& a_meVel, double a_prio, const Params& p)
	{
		return -Length(a_chest - (a_meChest + a_meVel * p.predT)) / std::max(1e-6, a_prio);
	}

	Vec3 ApproachVDes(const Vec3& a_meFeet, const Vec3& a_meChest, const Vec3& a_tFeet, const Vec3& a_tChest, double a_vmax, const Params& p,
		const Vec3& a_meVel)
	{
		if (Length(a_tChest - a_meChest) <= p.approachMin) return {};
		const Vec3   err{ a_tFeet.x - a_meFeet.x, a_tFeet.y - a_meFeet.y, Vert(a_tFeet.z - a_meFeet.z, p.hoverStep) };
		const double el = Length(err);
		if (el < 1e-3) return {};
		if (!(p.approachBrake > 0)) return err * (std::min(p.approachGain * el, a_vmax) / el);  // the L4D2 law
		// ROUND3 SPEED (S7): time-optimal braking curve toward the contact speed gain * approachMin (L4D2 arrival 620 u/s);
		// the gap is shortened by the closing speed x approachLagT (TABS drag lag), so he does not overshoot the target.
		const Vec3   dir = err / el;
		const double closing = sp::math::IsFinite(a_meVel) ? std::max(0.0, Dot(a_meVel, dir)) : 0.0;
		const double dEff = el - p.approachLagT * closing;
		const double vHit = p.approachGain * p.approachMin;
		double       s = p.approachGain * std::max(0.0, dEff);
		if (dEff > p.approachMin) s = std::max(s, std::sqrt(vHit * vHit + 2.0 * p.approachBrake * p.flyAccel * (dEff - p.approachMin)));
		return dir * std::min(s, a_vmax);
	}

	Vec3 RamVDes(const Vec3& a_meFeet, const Vec3& a_tFeet, const Vec3& a_tVel, double a_vmax, const Params& p, const Vec3& a_meVel)
	{
		const Vec3   flat{ a_tFeet.x - a_meFeet.x, a_tFeet.y - a_meFeet.y, 0.0 };
		const double fl = Length(flat);
		const bool   meV = sp::math::IsFinite(a_meVel), tV = sp::math::IsFinite(a_tVel);
		const double closing0 = (meV && fl > 1e-3) ? std::max(0.0, Dot(a_meVel, flat / fl)) : 0.0;
		const double lead = std::clamp(fl / std::max({ closing0, p.ramV, 1.0 }), 0.0, std::max(0.0, p.ramLead));
		const Vec3   tv = tV ? a_tVel * lead : Vec3{};
		const Vec3   err{ flat.x + tv.x, flat.y + tv.y, Vert(a_tFeet.z + tv.z - a_meFeet.z, p.hoverStep) };
		const double el = Length(err);
		if (el <= std::max(1e-3, p.ramStop)) return {};
		const Vec3   dir = err / el;
		const double closing = meV ? std::max(0.0, Dot(a_meVel, dir)) : 0.0;
		const double dEff = el - p.approachLagT * closing;
		const double s = std::sqrt(p.ramV * p.ramV + 2.0 * std::max(0.0, p.approachBrake) * p.flyAccel * std::max(0.0, dEff - p.ramStop));
		return dir * std::min(s, a_vmax);
	}

	Vec3 ClampLen(const Vec3& v, double a_max)
	{
		const double l = Length(v);
		return (l > a_max && l > kTiny) ? v * (a_max / l) : v;
	}

	Vec3 CruiseVDes(const Vec3& a_pos, const Vec3& a_aim, const Vec3& a_aimVel, const CruiseCfg& c)
	{
		const Vec3   d = a_aim - a_pos;
		const double dist = Length(d);
		Vec3         v = a_aimVel * c.ff;
		if (dist > 1e-3) {
			const double cap = std::min(c.vMax, std::sqrt(c.vHand * c.vHand + 2.0 * c.accel * std::clamp(c.brake, 0.05, 1.0) * std::max(0.0, dist - c.handR)));
			v += d * (cap / dist);
		}
		return v;
	}

	Vec3 CruiseStep(const Vec3& a_v, const Vec3& a_vDes, double a_accel, double a_dt, double a_maxV)
	{
		return ClampLen(a_v + ClampLen(a_vDes - a_v, a_accel * std::max(0.0, a_dt)), a_maxV);
	}

	void StuckLaw::Step(bool a_contact, double a_intent, double a_actual, double a_dt, const Params& p)
	{
		if (a_contact && a_intent > p.stuckIntent && a_actual < p.stuckRatio * a_intent) t += a_dt;
		else t = std::max(0.0, t - 2.0 * a_dt);
	}

	bool InAttachedCells(const Vec3& a_p, const Vec3& a_playerFeet, int a_uGrids, double a_cell)
	{
		if (a_cell <= 0) return true;
		const long long half = std::max(0, (a_uGrids - 1) / 2);
		auto cell = [a_cell](double a_v) { return static_cast<long long>(std::floor(a_v / a_cell)); };
		return std::llabs(cell(a_p.x) - cell(a_playerFeet.x)) <= half && std::llabs(cell(a_p.y) - cell(a_playerFeet.y)) <= half;
	}

	Plan PlanPath(const PlanIn& in, const Params& p, IWorld& w)
	{
		Plan pl;
		pl.kind = Plan::kFail;
		if (!in.exterior) return pl;
		const int budget = std::max(0, std::min(in.maxRays, static_cast<int>(p.planRays)));
		auto clear = [&](const Vec3& a_a, const Vec3& a_b) -> int {
			if (pl.rays >= budget) return -1;
			++pl.rays;
			return w.Clear(a_a, a_b) ? 1 : 0;
		};
		auto okCell = [&](const Vec3& a_q) { return !in.cellCheck || InAttachedCells(a_q, in.playerFeet, in.uGrids, p.cellSize); };
		const double chestUp = std::max(0.0, in.meChest.z - in.meFeet.z);
		const double tTop = in.tFeet.z + std::max(1.0, in.tHeight);
		const double base = std::max(in.meFeet.z, in.tFeet.z);

		// 0. already high (airborne re-plan): cross at his own height, no climb (keeps a re-plan from creeping up)
		if (in.meChest.z >= tTop + p.skyH1) {
			const Vec3 T{ in.tFeet.x, in.tFeet.y, in.meChest.z };
			if (okCell(T)) {
				const int r = clear(in.meChest, T);
				if (r == 1) {
					pl.kind = Plan::kOver;
					pl.wp[0] = T;
					pl.n = 1;
					pl.H = T.z - tTop;
					return pl;
				}
			}
		}

		// 1. sky heights, lowest first (BRAIN §6.4); an up-ray hit lowers the crossing under an overpass (HUNT §4.4)
		const double hs[3] = { p.skyH1, p.skyH2, p.skyH3 };
		double       sideZ = in.meChest.z;  // the lowest height he can reach straight up (for the side detour)
		bool         haveSide = false;
		bool         prevCross = false;     // FINISH A2 fix lane 5 (brain.skyMidH): the height below was tried and its crossing was blocked
		// FINISH A2 fix lane 5 (brain.skyMidH / skyRampDeg): at most kExtraRays more rays for the middle height and the ramp (a plan is
		// made at most every brain.replan s; the straight climb stays the answer when they run out)
		constexpr int kExtraRays = 5;
		int           extra = 0;
		auto          clearX = [&](const Vec3& a_a, const Vec3& a_b) -> int {
            if (extra >= kExtraRays) return -1;
            ++extra;
            ++pl.rays;
            return w.Clear(a_a, a_b) ? 1 : 0;
		};
		for (int k = 0; k < 3; ++k) {
			double h = hs[k];
			Vec3   P{ in.meChest.x, in.meChest.y, std::max(base + h + chestUp, in.meChest.z) };
			bool   upOk = true;
			if (P.z - in.meChest.z >= 1.0) {
				if (pl.rays >= budget) break;
				++pl.rays;
				double frac = -1;
				if (w.Cast(in.meChest, P, frac)) {
					upOk = false;
					if (frac >= 0) {
						const double hitZ = in.meChest.z + frac * (P.z - in.meChest.z);
						const double low = hitZ - p.overpassDrop;
						if (low >= in.meChest.z + p.overpassMin) {
							P.z = low;
							h = low - chestUp - base;
							pl.lowered = true;
							upOk = true;
						}
					}
				}
			}
			if (!upOk) break;  // a roof above him: higher crossings are blocked too
			if (!haveSide) {
				sideZ = P.z;
				haveSide = true;
			}
			Vec3 T{ in.tFeet.x, in.tFeet.y, pl.lowered ? P.z : tTop + h };
			if (!okCell(P) || !okCell(T)) {
				if (pl.lowered) break;
				prevCross = false;
				continue;
			}
			const int r = clear(P, T);
			if (r < 0) break;
			if (r == 1) {
				// FINISH A2 fix lane 5 (brain.skyMidH; the finA film S3: a 4.5 m house roof between him and the player - skyH1 2.8 m
				// blocked, skyH2 9.1 m clear, he crossed at 9-10 m): the crossing blocked one height down, this one clear: the middle
				// height too (the same vertical as P, below it: its up-ray is covered by P's)
				if (in.forReturn && p.skyMidH >= 0.5 && k >= 1 && prevCross && !pl.lowered) {
					const double hm = 0.5 * (hs[k - 1] + h);
					const Vec3   Pm{ P.x, P.y, std::max(base + hm + chestUp, in.meChest.z) };
					const Vec3   Tm{ T.x, T.y, tTop + hm };
					if (okCell(Pm) && okCell(Tm) && clearX(Pm, Tm) == 1) {
						P = Pm;
						T = Tm;
						h = hm;
						pl.mid = true;
					}
				}
				pl.kind = Plan::kOver;
				int n = 0;
				if (P.z - in.meChest.z >= p.wpReach) {
					// FINISH A2 fix lane 5 (brain.skyRampDeg; the finA gate film S3 1.22-1.54 s: the return plan's climb point straight above
					// him - 9.8 m up at 68-77 deg with 2-3 m of travel, body upright, vertical trails): the climb is a RAMP along the way to
					// the crossing - its waypoint ahead of him (toward T) at the crossing height, at most skyRampDeg steep, then at most
					// skyRampMaxDeg; both legs (him -> ramp point -> T) a clear line, the point within the attached cells and short of T.
					// A wall too near for any of them (or no rays left): the straight climb as before.
					bool         ramped = false;
					const double dz = P.z - in.meChest.z;
					const Vec3   fd = FlatDir(T - in.meChest);
					const double flatT = Length(Flat(T - in.meChest));
					if (in.forReturn && p.skyRampDeg > 0 && Length(fd) > 0.5 && std::isfinite(flatT)) {
						const double angs[2] = { p.skyRampDeg, std::max(p.skyRampDeg, p.skyRampMaxDeg) };
						for (int a = 0; a < 2 && !ramped; ++a) {
							if (a == 1 && !(angs[1] > angs[0])) break;
							const double ang = std::clamp(angs[a], 1.0, 85.0) * sp::math::kDeg2Rad;
							const double run = dz / std::tan(ang);
							if (!(run >= p.wpReach) || run > flatT - p.wpReach) continue;  // (a ramp past T, or one that is a climb point)
							const Vec3 R{ in.meChest.x + fd.x * run, in.meChest.y + fd.y * run, P.z };
							if (!okCell(R)) continue;
							const int r1 = clearX(in.meChest, R);
							if (r1 < 0) break;
							if (r1 == 0) continue;
							const int r2 = clearX(R, T);
							if (r2 < 0) break;
							if (r2 == 1) {
								pl.wp[n++] = R;
								pl.ramp = true;
								ramped = true;
							}
						}
						if (!ramped) pl.rampNo = true;
					}
					if (!ramped) pl.wp[n++] = P;
				}
				pl.wp[n++] = T;
				pl.n = n;
				pl.H = h;
				return pl;
			}
			if (pl.lowered) break;  // under an overpass: nothing higher to try
			prevCross = true;
		}

		// 2. side detour at the lowest reachable height: yaw +-sideYaw1, then +-sideYaw2, waypoint at sideFrac
		const Vec3   flat = FlatDir(in.tChest - in.meChest);
		const double dist = Length(Flat(in.tChest - in.meChest));
		if (Length(flat) > 0.5 && dist > 1.0) {
			const double len = std::min(dist, p.sideLen);
			const double yaws[4] = { p.sideYaw1, -p.sideYaw1, p.sideYaw2, -p.sideYaw2 };
			const Vec3   S{ in.meChest.x, in.meChest.y, sideZ };
			for (double yaw : yaws) {
				const Vec3 dir = RotateZ(flat, yaw);
				const Vec3 E = S + dir * len;
				const Vec3 W = S + dir * (len * p.sideFrac);
				if (!okCell(E)) continue;
				const int r = clear(S, E);
				if (r < 0) break;
				if (r == 0) continue;
				// the detour must lead somewhere: from the waypoint, the crossing point above the target is in reach
				const Vec3 T{ in.tFeet.x, in.tFeet.y, std::max(tTop + p.skyH1, W.z) };
				const int  r2 = clear(W, T);
				if (r2 < 0) break;
				if (r2 == 1) {
					pl.kind = Plan::kSide;
					int n = 0;
					if (S.z - in.meChest.z >= p.wpReach) pl.wp[n++] = S;
					pl.wp[n++] = W;
					pl.n = n;
					pl.H = S.z - base;
					return pl;
				}
			}
		}
		pl.kind = Plan::kFail;
		pl.n = 0;
		return pl;
	}

	// Match an actual failed landing's floor band, never the identity of a foe that can move to another floor.
	static bool RefusedBlinkLanding(const Vec3& a_spot, const Vec3* a_refused, double a_radius)
	{
		return a_refused && a_radius > 0 && std::abs(a_spot.z - a_refused->z) <= 64.0 &&
		       Length(Flat(a_spot - *a_refused)) <= a_radius + kTiny;
	}

	static bool BlinkProtectsPlayer(const Cand& c, const Params& p, double now)
	{
		return c.attackingPlayer || (c.lastHitPlayerAt > -1e8 && now >= c.lastHitPlayerAt - kTiny &&
		       now - c.lastHitPlayerAt <= std::max({ p.hitMemory, p.attackerValid, p.d2DbHitWin }) + kTiny);
	}

	bool BlinkSpot(const Cand& a_t, const Vec3& a_meFeet, const Params& p, IWorld& w, Vec3& a_outFeet, int& a_rays, int a_maxRays,
		const Vec3* a_refusedFeet, double a_refusedR, bool* a_rejected)
	{
		a_rays = 0;
		Vec3 away = FlatDir(a_meFeet - a_t.feet);
		if (LengthSq(away) < 0.5) away = { 1, 0, 0 };
		const Vec3   side{ -away.y, away.x, 0 };
		const Vec3   dirs[4] = { away, side, side * -1.0, away * -1.0 };
		const double chestUp = a_t.chest.z - a_t.feet.z;
		for (const Vec3& d : dirs) {
			if (a_rays + 2 > a_maxRays) break;
			const Vec3 spot = a_t.feet + d * p.blinkDist;
			++a_rays;
			if (!w.Clear(a_t.chest, spot + Vec3{ 0, 0, chestUp })) continue;  // a wall right there
			Vec3 fl{};
			++a_rays;
			if (!w.Floor(spot + Vec3{ 0, 0, p.blinkFloorUp }, p.blinkFloorDrop, fl)) continue;  // no floor (ledge)
			if (RefusedBlinkLanding(fl, a_refusedFeet, a_refusedR)) {
				if (a_rejected) *a_rejected = true;
				continue;
			}
			a_outFeet = fl;
			return true;
		}
		return false;
	}

	bool InLane(const Vec3& a_victim, const Vec3& a_dir, const Vec3& a_p, const Params& p) { return InLane(a_victim, a_dir, a_p, p, p.laneLen); }

	// FINISH A2 fix lane 4 round 2: the same lane out to a_len u (the sentry-bot blast pick asks it for the corpse's own path only)
	bool InLane(const Vec3& a_victim, const Vec3& a_dir, const Vec3& a_p, const Params& p, double a_len)
	{
		const Vec3   rel = a_p - a_victim;
		const double along = Dot(rel, a_dir);
		if (along <= 0 || along > a_len) return false;
		const double perp = Length(rel - a_dir * along);
		if (perp <= p.laneR) return true;
		return std::atan2(perp, along) * sp::math::kRad2Deg <= p.laneConeDeg;
	}

	LaneOut FlingLane(const Vec3& a_victim, const Vec3& a_dir, std::span<const Vec3> a_protected, const Params& p)
	{
		LaneOut o;
		o.dir = Normalize(a_dir, { 1, 0, 0 });
		auto blockedBy = [&](const Vec3& a_d) -> int {
			for (std::size_t i = 0; i < a_protected.size(); ++i)
				if (InLane(a_victim, a_d, a_protected[i], p)) return static_cast<int>(i);
			return -1;
		};
		o.blocker = blockedBy(o.dir);
		if (o.blocker < 0) return o;
		o.changed = true;
		const double tries[7] = { 30, -30, 60, -60, 90, -90, 180 };
		for (double deg : tries) {
			const Vec3 d = RotateZ(o.dir, deg);
			if (blockedBy(d) < 0) {
				o.dir = d;
				o.rotatedDeg = deg;
				return o;
			}
		}
		o.speedScale = p.laneSlow;
		return o;
	}

	// ============================================================================================ the brain
	struct Brain::Ctx
	{
		double now{ 0 }, dt{ 0 };
		Vec3   centre{}, playerFeet{};
		double R{ 0 };
		int    range{ 1 };
		double farR{ 0 }, maxR{ 0 }, abort{ 0 };
		double health{ 1 };
		bool   farOpen{ false }, preemptOpen{ false }, stale{ true }, waiting{ false }, dialogue{ false }, busy{ false };
		bool   sneaking{ false };
		bool   staleNative{ false };  // ROUND3 fix 2: stale verdicts (the VM is slow) -> first strikes on the native checks
		const char* farWhy{ "" };
		double dMeCentre{ 0 }, dMePlayer{ 0 };
		Vec3   meChest{};  // ROUND3 fix 5: the big-creature token rule measures from him
		bool   interiorHunt{ false };  // ROUND5: his real interior cell with the navmesh on: hidden idle hostiles there are hunted
		bool   farHard{ false };       // HOTFIX round5C: the far tiers are closed by range 0 / waiting / mounted (defendFarOpen never opens these)
		bool   fpHereOn{ false };      // FINISH A2 fix lane 2 r2 (brain.fpHereR): the rule is on here (indoors)
		bool   plQuiet{ false };       // ... and no candidate attacks the player / hit it within hitMemory s
	};

	Brain::Brain() { Reset(); }

	void Brain::Reset()
	{
		_mem.clear();
		_info.clear();
		_vet.clear();
		_lastIds.clear();
		_lastAtk.clear();
		_lastPlAtk.clear();  // FINISH B2 fix lane 5
		_st = Stats{};
		_target = 0;
		_tTier = Tier::kNone;
		_tFar = false;
		_reReachId = 0;  // s7.5 fix lane 3
		_reReachUntil = -1e9;
		_prot.Reset();  // R6 step 8.1
		_nsBlockId = 0;  // s13 fix round 3 lane 4
		_nsBlockUntil = -1e9;
		_cdId = 0;  // explore m1 fix lane 3
		_cdAt = -1e9;
		_czId = 0;  // FINISH lane A_combat_zone (brain.czDefendT / czEssHoldT)
		_czBest = 1e18;
		_czAt = _czSeen = _czPressAt = -1e9;
		_czEssId = 0;
		_czEssSince = _czEssLast = -1e9;
		_guardTryId = 0;  // FINISH A2 fix lane 5 (brain.turnedGuardR)
		_guardTryAt = -1e9;
		ResetTargetState();
		_returning = _recallFly = _returnBlocked = false;
		_returnSince = _returnNoPathSince = -1;
		_noReturnUntil = -1e9;
		_returnClear = true;
		_returnCheckAt = -1e9;
		_chainT = _restT = 0;
		_chainLock = false;
		_lastFarEndAt = -1e9;
		_lastFarTier = Tier::kNone;
		_lastPutBackAt = _lastBlinkAt = _lastBlinkTryAt = -1e9;
		_djPendId = 0;  // s4 fix lane 2
		_djPendAt = _djTryAt = -1e9;
		_stuck.Reset();
		_stuckFor = _stuckFollowFor = 0;
		_rr = 0;
		_lastVetReqAt = -1e9;
		_returnBlocked = _followRayBlocked = false;  // ROUND3 SPEED (S4)
		_followBlockedT = 0;
		_followRayAt = _blockedReturnUntil = -1e9;
		_returnStuck = false;  // ROUND5 A2
		_stuckReturnAt = -1e9;
		_swChallId = 0;  // REVIEW-B / DPREP-T
		_swSince = 0;
		_attackHoldN = _switchHoldN = _fpDropN = 0;
		_d2SlowSwN = 0;  // D2 tuner
		_d2DbN = _d2EssSkipN = 0;
		_essHurtPickN = 0;  // completion side lane 1 (brain.essHurtFirst)
		_dbLockId = 0;
		_dbLockUntil = -1e9;
		_s2fLockId = 0;  // step-2 fixer
		_s2fLockUntil = -1e9;
		_s2fBandDN = _s2fBigN = _s2fDefBlinkN = 0;
		_cageGuardN = _cageSkipN = 0;  // s4 fix lane 2
		_cageHandId = 0;               // s11 fix lane 1
		_cageHopKeepN = 0;
		_bigAimN = 0;  // s12 fix lane 2
		_atkKeepN = 0;  // s12 fix r2 lane 2
		_flipKeepN = _preNoTokN = _capBlinkN = 0;  // s12 fix r3 lane 3
		_bigAimHoldId = 0;
		_fpFarN = _fpArriveN = _protNearN = 0;  // s12 r2 fix lane 1
		_fpHereN = 0;                            // FINISH A2 fix lane 2 r2
		_d2PressLastId = 0;
		_retOpenAt = -1e9;
		_retEndOff = _retWait = false;  // FINISH B2 Fable
		_retWaitAt = _blinkLandAt = _undoneAt = -1e9;
		_blinkLandId = _undoneId = 0;
		_blinkLandConfirmed = false;
		_plFeetHave = false;
		_plArriveAt = -1e9;
		_protNearCur = false;
		_rcStallId = 0;  // s12 r2 lane 4
		_rcStallSince = -1e9;
		_rcStallKeeps = 0;
		_noIdleSel = false;     // FINISH B2 fix lane 4 (brain.noIdleNoResume)
		_plAtkNearAt = -1e9;    // FINISH B2 fix lane 4 (brain.protectBombHidLatch)
		_plAtkNearId = 0;
		_burst = false;  // s12 r3 fix lane 1
		_plHurtNow = false;  // FINISH A2 lane 3
		_atkMe1Id = 0;  // FINISH A2 lane 3 round 2
		_atkMe1 = _atkMe2 = 1e18;
		_atkNearPl = false;  // finB (REVIEW 2)
		_wtId = _wtStraight = 0;
		_wtHeld = 0;
		_wtAt = -1e9;
		_burstLosRr = 0;
		_burstKeepN = _burstHidN = _blinkHoldN = 0;
		_burstBigN = 0;  // s12 r3 integrator
		_chainNearN = 0;  // s13 r3 lane 5
		_chainNearId = 0;
		_rcStallSinceW = _rcProgSeen = _rcProgW = -1e9;  // s12 r3 lane 5
		_rcWallKeeps = 0;
		_resetPending = true;  // completion side lane 4 s13r2 (brain.newcomerKnown): the next Update's candidates were already there
		_resetAt = -1e9;
		_knownIds.clear();
		_newcomerOldN = _repeatDefOkN = _preAimKeepN = 0;
		_newcomerOldBigN = _newcomerFightN = 0;  // completion side 3 s13r3
		_newcomerOldBigLast = 0;
		_onLeg = false;
		_blinkKeepId = 0;  // DPREP-FOLLOW
		_blinkKeepUntil = -1e9;
		_pbSeenSince = _pbRefusedAt = -1e9;
		_fbId = 0;  // HOTFIX round5C
		_recallAt = -1e9;
		_essHoldIds.clear();  // HOTFIX2
		_essOff = 0;  // D1H (_mem, with the essential history, is cleared above)
		_essWallOn = false;
		_d2PbSince = _d2PbAt = -1e9;  // D2
		_d2PbN = 0;
		_extLostTryAt = -1e9;  // step 4 fix lane 1
		std::fill(std::begin(_d2BlinkAt), std::end(_d2BlinkAt), -1e9);
		_barsId = 0;
		_barsAt = -1e9;
		_barsSince = -1;
		_barsOk = false;
		_retNavShutUntil = -1e9;  // R6 s10 fix lane 3
		TeleShutClear();          // completion lane 3
		_crumbTryAt = -1e9;
		_nearRayAt = _nearAt = _nearNavUntil = -1e9;
		_nearClear = false;
		CrumbClear();
	}

	void Brain::OnLoad()
	{
		Reset();
		_deny.clear();
		_everVerdict = false;
		_lastVerdictAt = -1e9;
		_verdictHealth = 1.0;
		_verdictGap = 0;
	}

	void Brain::ResetTargetState()
	{
		_atPlRecovery.pending = false;
		_retEndOff = _retWait = false;  // cx2: combat takeover cannot carry a no-target endpoint wait
		_tSince = _now;
		_heldT = 0;
		_best = _farBest = 1e18;
		_progAt = _farProgAt = _now;
		_farFails = 0;
		_stuckSince = -1;
		_pinBlockedSince = -1;
		_chaseSent = false;
		_attackSent = false;
		_planFor = 0;
		_planForReturn = false;
		_plan = Plan{};
		_wpIdx = 0;
		_planAt = -1e9;
		_noPathSince = _exhaustedSince = -1;
		_planGoal = {};
		_lookHit = false;
		_aiChasing = false;
		// ROUND3 REACH
		_nav = NavState{};
		_stage = -1;
		_stageAt = _now;
		_stillT = _hiddenT = 0;
		_directBlocked = 0;
		_directAt = -1e9;
		_chainAt = _directClearAt = _directBlockAt = -1e9;
		_reachBlinks = 0;
		_aiBest = 1e18;
		_aiProgAt = _now;
		_stageNoted = false;  // s4f (brain.extStageFail)
		_wtStraight = 0;      // FINISH A2 lane 3 round 2 (brain.windowTrapT: no straight flight at the new target yet)
		_triedNav = _triedRay = _doorAi = _aiTried = false;
		_doorT = 0;
		_aiFrom = {};
		_stageWhy = "";
		_noHitSince = -1;
		_stuckChainUntil = -1e9;  // ROUND4
		_noReachSince = -1;
		_hiddenTotal = 0;
		_chainD0 = 1e18;  // ROUND4 fix 2
		_pushSide = 0;    // ROUND5
		_pushSwaps = 0;
		_pushHead = {};
		_pushHeadSwaps = -1;
		_pushDrop = 0;
		_pushSideAt = _pushFreeAt = _pushFwdUntil = -1e9;
		_pushBest = 1e18;
		_pushProgAt = _now;
		_pushFor = 0;
		_pushRealProgAt = _now;  // ROUND5 A2
		_pushRealBest = 1e18;
		_pushAimIdx = -1;
		_tHitAt = -1e9;
		_nearSince = -1;
		_capBlinkTried = false;
		_routeId = 0;  // R6 step 9
		_teleShieldId = 0;  // R6 step 10
		_telePinId = 0;
		_holdId = 0;  // DPREP-T: a new target gets its own attack hold
		_holdSince = -1;
		_barsId = 0;  // D2: the bars approach is per target (its fail rest is kept on the foe: Mem::barsFailUntil)
		_barsSince = -1;
		_barsOk = false;
		_nsId = 0;  // s13 fix round 3 lane 4: a stall leg is per target (the 2 s block after a run-out stays on its foe)
		_nsUntil = -1e9;
	}

	// ROUND5 A2 (gate A1, museum upstairs at 60 fps: every re-pick restarted the chain at "-1->0 why=nav" under the
	// balcony, 6 times, never killed): the furthest stage the chain reached on a foe (2 push, 3 blink) is kept on it, so a
	// re-pick within huntIgnore s (the foe moved < chainMemMove u) goes on from there. A hit on it clears it.
	void Brain::NoteChain(std::uint32_t a_id, const Vec3& a_feet)
	{
		if (_stage < 2 || _stage > 3 || a_id == 0) return;
		Mem& m = M(a_id);
		if (m.chainStage < _stage || _now - m.chainAt > _p.huntIgnore) m.chainStage = _stage;
		m.chainAt = _now;
		m.chainFeet = a_feet;
	}

	void Brain::NoteEnd(const char* a_why)
	{
		if (_target && _tFar) {
			_endId = _target;
			_endWhy = a_why;
		}
	}

	void Brain::DropTarget()
	{
		_target = 0;
		_tTier = Tier::kNone;
		_tFar = false;
		ResetTargetState();
	}

	void Brain::OnTeleport(bool a_own)
	{
		_plAtkNearAt = -1e9;
		_plAtkNearId = 0;
		// FINISH B2 Fable (brain.blinkUndoneT): the ENGINE moved him within kUndoneWin s of his own reach blink = it refused that landing
		// (finA2 soak K3 20:30:30.6-32.6: three blinks onto the Speakeasy shell at z 1136, each 'TELE|jump why=flagged' back to the floor
		// 12-19 ms later, the next chain blinking there again): the landing and its foe are remembered; the BLINK stage refuses blinks
		// there for blinkUndoneT s (ReachHidden). His own jumps (a_own: a blink, the return / lost transfer) never count.
		if (!a_own && _blinkLandConfirmed && _p.blinkUndoneT > 0 && _p.blinkUndoneR > 0 && _blinkLandAt > -1e8 && _now >= _blinkLandAt - kTiny && _now - _blinkLandAt <= kUndoneWin + kTiny) {
			// a landing he punched the foe from worked (sp_tests C8-02b, brain.blinkRepeatDefend: the fall after the punch is the raider's /
			// the engine's doing, not a refused landing): only a landing with no hit counts as undone
			const auto it = _mem.find(_blinkLandId);
			const bool landed = it != _mem.end() && it->second.hitByMeAt >= _blinkLandAt - kTiny && it->second.hitByMeAt <= _now + kTiny;
			if (!landed) {
				_undoneLand = _blinkLand;
				_undoneId = _blinkLandId;
				_undoneAt = _now;
				++_st.blinkUndone;
			}
		}
		_blinkLandAt = -1e9;
		_blinkLandConfirmed = false;
		_retEndOff = _retWait = false;  // FINISH B2 Fable: a new place - the way back starts over
		NoteEnd("teleport");
		DropTarget();
		_prot.dropped = 0;  // R6 step 8.1: a jump starts the protect clocks over (the counters stay)
		_prot.droppedAt = _prot.switchAt = -1e9;
		_prot.passId = 0;  // fix lane 1: no R0h hold across a jump
		_prot.urgentId = 0;  // step 10 fix lane 1: nor R5's looser bar
		_wasFarLeg = false;
		_stuck.Reset();
		_stuckFor = _stuckFollowFor = 0;
		_returning = _recallFly = _returnBlocked = false;
		_returnSince = _returnNoPathSince = -1;
		_returnClear = true;
		_returnCheckAt = -1e9;
		_lowClear = false;
		_clearAt = -1e9;
		_noReturnUntil = -1e9;
		_returnBlocked = _followRayBlocked = false;  // ROUND3 SPEED (S4)
		_followBlockedT = 0;
		_followRayAt = _blockedReturnUntil = -1e9;
		_returnStuck = false;  // ROUND5 A2
		_blinkKeepId = 0;      // DPREP-FOLLOW
		_blinkKeepUntil = -1e9;
		_pbSeenSince = -1e9;
		_d2PbSince = -1e9;  // D2: he was moved: a new put-back episode
		_d2PbN = 0;
		_retNavShutUntil = -1e9;  // R6 s10 fix lane 3: a new place - the old door and the old trail mean nothing here
		TeleShutClear();          // completion lane 3: and the old shut ways neither
		CrumbClear();
	}

	// DPREP-FOLLOW (GATE_B fail 2, "a blink followed at once by 'give up, unreachable'", 11-12 s kills in 2 of 16 runs:
	// c120-r4m-b 23:49:24.97 'NAV stage 0->4 why=blink d=106' + 'brain give up 00018D6B unreach' in the same frame). His own
	// reach / pin / hunt blink went through OnTeleport: the target was dropped ('HUNT|end why=teleport'), the re-pick resumed
	// the chain at the BLINK stage from the chain memory (chainMemory), and at d 106 < navBlinkNear the blink stage could only
	// give up. brain.blinkKeep 1: the target, its tier and its blink count are kept, the chain and its memory on that foe
	// restart (-1), the stuck / progress clocks restart from the landing, and for blinkKeepFor s a seen target within
	// blinkAttackR is attacked straight (ChooseMove: the TABS dash, the contact hit). 0 = round 5 B (OnTeleport).
	void Brain::OnOwnBlink(std::uint32_t a_target)
	{
		if (_atPlRecovery.pending && a_target == _atPlRecovery.target) {
			const auto& r = _atPlRecovery;
			CommitReachBlink(a_target, _p, r.capDef, r.s2fExtDef, r.hurtNow, r.thrNow);
		}
		_atPlRecovery.pending = false;  // runtime confirmed that the proposed move happened
		// DidBlink calls this only after the runtime actually performed the move. A request refused by DoBlink is not a landing.
		const bool committedLanding = a_target != 0 && a_target == _blinkLandId && _blinkLandAt > -1e8 && std::abs(_now - _blinkLandAt) <= kTiny;
		const Vec3 landingFeet = _blinkLand;
		const double landingAt = _blinkLandAt;
		if (committedLanding) _blinkLandConfirmed = true;
		_czId = 0;  // FINISH lane A_combat_zone (brain.czDefendT): the no-progress clock starts over from where his blink put him
		// s4 fix lane 2 (REVIEW: DefendJump vs DoBlink refusals): the Warden's defend jump asked in this brain frame really
		// happened -> commit it now (it was only asked in DefendJump): the blink time, the D2 blink budget, the count; the reach
		// chain and the progress / stuck clocks restart below (ResetTargetState), the stage says why.
		const bool dj = _djPendId != 0 && a_target == _djPendId && std::abs(_now - _djPendAt) <= 1e-9;
		_djPendId = 0;
		if (dj) {
			_lastBlinkAt = _now;
			D2NoteBlink();
			++_st.extDefendJumps;
		}
		if (_p.blinkKeep < 0.5 || a_target == 0 || a_target != _target) {
			OnTeleport(true);  // own reset; retain only the confirmed landing's provenance
			if (committedLanding) {
				_blinkLand = landingFeet;
				_blinkLandAt = landingAt;
				_blinkLandId = a_target;
				_blinkLandConfirmed = true;
			}
			return;
		}
		++_st.blinkKeeps;
		const std::uint32_t keep = _target;
		const Tier          tier = _tTier;
		const bool          far = _tFar;
		const int           blinks = _reachBlinks;
		const bool          capTried = _capBlinkTried;
		const double        tSince = _tSince;
		const bool          attackSent = _attackSent;
		ResetTargetState();  // stage -1, the nav state, the stuck / progress / push clocks from now
		_target = keep;
		_tTier = tier;
		_tFar = far;
		_reachBlinks = blinks;  // the per-target blink cap still holds
		_capBlinkTried = capTried;
		_tSince = tSince;
		_attackSent = attackSent;  // one 'HUNT|attack' edge per hunt (not a second one after the blink)
		Mem& m = M(keep);
		m.chainStage = -1;  // no 'resume' at the BLINK stage
		m.blindT = -1;
		_wasFarLeg = false;
		_stuck.Reset();
		_stuckFor = _stuckFollowFor = 0;
		_returning = _recallFly = _returnBlocked = _returnStuck = false;
		_returnSince = _returnNoPathSince = -1;
		_blinkKeepId = keep;
		_blinkKeepUntil = _now + _p.blinkKeepFor;
		if (dj) _stageWhy = "defendJump";  // s4 fix lane 2 (the stage itself is -1: a fresh plan after the jump)
	}

	// DPREP-FOLLOW (tabs-spec G10, GATE_B fail 4): the blocked-follow / stuck-follow return, navmesh leg first when it can
	// route him (a_navOk), else a_noNavEsc (0 sky / side plan, 2 flown push). a_stuck: it ends only at returnDoneNear or on
	// a clear line after 120 u of progress (a line clear from the capsule is no proof the sim can follow again).
	bool Brain::StartBlockedReturn(double a_dSpot, bool a_navOk, bool a_stuck, int a_noNavEsc)
	{
		_returning = true;
		_returnBlocked = true;
		_returnStuck = a_stuck;
		_returnClear = false;
		_returnSince = _now;
		_returnNoPathSince = -1;
		_returnCheckAt = -1e9;
		if (_nav.forId == kReturnNavId) _nav = NavState{};  // a fresh plan (re-plan)
		_retBest = _retStart = a_dSpot;
		_retProgAt = _now;
		// R6 s10 fix lane 3 (brain.retShutHold; r5 museum 16:47:27-35: every refused put-back of the locked way back started this
		// return with the SAME navmesh leg into the same locked door, 18 times): while the way back is shut, the flown push
		const bool shut = _p.retShutHold > 0 && _now < _retNavShutUntil && _retNavShutUntil - _now <= _p.retShutHold + kTiny;
		_retEsc = (a_navOk && !shut) ? 1 : a_noNavEsc;
		_pushFor = 0;
		_retEndOff = _retWait = false;  // FINISH B2 Fable: a fresh way back
		++_st.followBlockedNavs;
		return true;
	}

	Brain::Mem& Brain::M(std::uint32_t a_id) { return _mem[a_id]; }

	void Brain::NoteHit(std::uint32_t a_id, double a_now, double a_distFromMe)
	{
		Mem& m = M(a_id);
		m.hitByMeAt = a_now;  // completion side lane 4 s13r2 (brain.blinkRepeatDefend: did his last blink at it give a hit)
		// cx2: an engine move did not prove that the ensuing combat was useless.
		// The B2 perch replay can hit a foe after the move back; keeping its old
		// failed-region cache then withheld the remaining useful group kills.
		// Any actual combat progress after that event invalidates this heuristic.
		// Release only our dedicated landing holds, never a generic Ignore.
		if (_p.blinkUndoneT > 0 && _p.blinkUndoneR > 0 && _undoneAt > -1e8 && std::isfinite(a_now) && a_now >= _undoneAt - kTiny) {
			_undoneAt = -1e9;
			_undoneId = 0;
			for (auto& entry : _mem) {
				entry.second.undoneUntil = -1e9;
				entry.second.undoneRefusalAt = -1e9;
			}
		}
		// explore cycle m1 fix lane 1 (brain.spentHurtT; EXPLORE cyclem1 A2c "never 'spent' a foe that hit the player in the last 3 s"): the
		// current target that hit the player within spentHurtT s is not made "spent" by his punch - a lethal punch marks it kill-pending at
		// once anyway, so this keeps only a SURVIVOR that is hurting the player as his target (he does not pick a far foe meanwhile).
		// Essentials keep the spent window: the engine flags their knock-down up to ~1 s later (essDownHold, s12 fix round 3 lane 2)
		const bool hurtKeep = _p.spentHurtT > 0 && a_id == _target && !m.essOnly && m.plHit[0] > -1e8 && a_now >= m.plHit[0] - kTiny &&
		                      a_now - m.plHit[0] <= _p.spentHurtT + kTiny;
		if (a_distFromMe > _p.spentNear && hurtKeep) ++_st.spentHurtKeeps;
		if (a_distFromMe > _p.spentNear && !hurtKeep) m.spentUntil = a_now + _p.spentIgnore;  // R3: one that did not fly stays a target
		// s13 fix r1 lane 2 (brain.essHoldStay): his control hit on the held essential he is on - he stays on it (Classify keeps it
		// his target) through the window (essHoldGap s, at least spentIgnore), with no new hit until it ends or it shoots the player
		if (_p.essHoldStay >= 0.5 && a_id == _target && EssHolding(a_id)) {
			const double gap = std::max(_p.spentIgnore, _p.essHoldGap);
			m.spentUntil = std::max(m.spentUntil, a_now + gap);
			m.essStayAt = a_now;
			m.essStayUntil = a_now + gap;
		}
		m.chainStage = -1;  // ROUND5 A2: reached it
		if (a_id == _target) {
			_heldT = 0;
			_noHitSince = -1;  // ROUND3 REACH stand-off breaker: it landed
			_noReachSince = -1;  // ROUND4: a hit is reach
			_tHitAt = a_now;     // ROUND5 A2
		}
	}

	void Brain::Ignore(std::uint32_t a_id, double a_now, double a_forS)
	{
		Mem& m = M(a_id);
		m.ignoreUntil = std::max(m.ignoreUntil, a_now + a_forS);
		m.giveUpAt = a_now;  // FINISH B2 fix lane 5: a give-up's ignore is never lifted by the recall keep
		if (a_id == _target) {
			NoteEnd("ignored");
			DropTarget();
		}
	}

	// HOTFIX round5C C (live round5C 03:30-03:39: essential Combat Zone raiders 00002F09 / 00002F1F and 000199D5 knocked down
	// every ~3 s - 8 knock-downs in 22 s - while killable foes shot the player): every knock-down of an essential foe is noted
	// (the runtime's NativeDamage spared it). Classify / Update rest it essRest s while killable foes are around and cap it
	// at essMaxPerMin knock-downs per 60 s.
	// HOTFIX2 (smoke run hf3 05:32:19.509 / .538: two 'spared' lines 29 ms apart for ONE knock-down of FF0010C9 - two
	// delayed hits landed before the engine flagged the bleedout - so it was capped after one knock-down and he idled
	// 10 s while it shot the player): a spared hit within essDedupe s of the last noted knock-down of that foe is the same
	// bleedout event and is not counted again (the runtime also skips a spare when the foe was at hp <= 0 already).
	// D1H (smoke F4): a_now is BRAIN time; the history is stamped on the essential clock (EssClock: the wall clock when
	// brain.essWallClock is on), so the 2-a-minute cap, essRest and essDedupe count real seconds at any frame rate.
	bool Brain::NoteSpared(std::uint32_t a_id, double a_now)
	{
		if (!a_id) return false;
		Mem&         m = M(a_id);
		const double t = EssClock(a_now);
		const double last = *std::max_element(std::begin(m.essAt), std::end(m.essAt));
		if (_p.essDedupe > 0 && last > -1e8 && t >= last && t - last < _p.essDedupe) {
			++_st.essDupes;
			return false;
		}
		auto* oldest = std::min_element(std::begin(m.essAt), std::end(m.essAt));
		*oldest = t;
		m.seenAt = std::max(m.seenAt, a_now);  // brain clock
		++_st.essDowns;
		// s12 fix round 3 lane 2 (brain.essDownHold; s12r2 gate hf 130907 HF3 0.29): it is down now - the engine flags its bleedout
		// up to ~1 s later and the spent rule (0.6 s from the punch) ran out first, so he picked it again and his second punch only
		// flung it ~900 u away. It is "spent" until the hold ends (no pick, no no-idle pick, it still counts as up for the far-hunt
		// hold); a shot at the player after the knock-down ends the hold at once (Update)
		m.essDownAt = a_now;  // (completion side lane 1, brain.essHurtFirst, reads it too; only the essDownHold hold below acts on it there)
		if (_p.essDownHold > 0) {
			m.essDownUntil = a_now + _p.essDownHold;
			m.spentUntil = std::max(m.spentUntil, m.essDownUntil);
		}
		if (a_id == _target) {
			NoteEnd("spared");
			DropTarget();
		}
		return true;
	}
	bool Brain::EssHolding(std::uint32_t a_id) const { return a_id && std::find(_essHoldIds.begin(), _essHoldIds.end(), a_id) != _essHoldIds.end(); }
	bool Brain::EssStaying(std::uint32_t a_id, double a_now) const
	{
		if (!a_id || _p.essHoldStay < 0.5) return false;
		const auto it = _mem.find(a_id);
		return it != _mem.end() && it->second.essStayUntil > a_now;
	}
	int Brain::EssDowns(std::uint32_t a_id, double a_now) const
	{
		const auto it = _mem.find(a_id);
		if (it == _mem.end()) return 0;
		const double e = EssClock(a_now);  // D1H: the essential clock
		int          n = 0;
		for (double t : it->second.essAt)
			if (e - t <= 60.0 && e >= t) ++n;
		return n;
	}
	bool Brain::EssCapped(std::uint32_t a_id, double a_now) const
	{
		return _p.essMaxPerMin > 0 && EssDowns(a_id, a_now) >= static_cast<int>(_p.essMaxPerMin);
	}
	std::string Brain::DefendWhy(std::span<const Cand> a_cands, const Vec3& a_me) const
	{
		const Cand* best = nullptr;
		double      bd = 1e18;
		for (const auto& c : a_cands) {
			if (!c.attackingPlayer || c.id == _target) continue;
			const double d = Length(c.chest - a_me);
			if (d < bd) {
				bd = d;
				best = &c;
			}
		}
		if (!best) return "-";
		const char* w = WhyNot(best->id);
		return std::string(w && w[0] ? w : "ok") + ":" + std::to_string(static_cast<long long>(bd));
	}

	void Brain::Recall(bool a_fly, double a_now)
	{
		_plAtkNearAt = -1e9;
		_plAtkNearId = 0;
		// BRAIN2 (INTEGRATOR, shadow review SR1-1): a put-back (stuck, inside solid, rule 1, whistle) also comes through
		// here. Ignoring EVERY candidate for recallIgnore made him stand next to the player for 3 s while a raider shot
		// them. Live attackers (hit the player / him / a teammate within attackerValid, inside attackerGuard) stay
		// pickable; far candidates and the dropped target are still ignored. brain.recallKeepAttackers = 0: old rule.
		// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR; the finB gate's P21 > 2 s: r5 20261027-040919 04:12:00.930-03.270 3.34 s and r3m
		// 20261027-040410 04:07:13.042-15.055 3.01 s, every 1 Hz sample 'tgt=00000000 dp=1 dw=ignored:<936-2573>' 0.25 / 0.38 s after the
		// controller's 'native|recall fly=False why=teleport' - the player moved mid-hunt - so the raider attacking the player, the dropped
		// hunt target itself in r5 (dp=1, dw=- before), had not HIT anyone within attackerValid, was no live attacker and sat on the
		// ignore list for recallIgnore 3 s while he stood by the player; all 3 candidate stretches > 2 s and 12 of the installed build's 46
		// are this recall window): a foe attacking the player (its combat target the player) within recallKeepPlAtkR u of the player
		// stays pickable too - the dropped target as well when the recall is the controller's hard stop (fly false: the player's teleport,
		// a Papyrus put-back / placement), unless it follows his OWN put-back request within kRecallOwnPb s (he could not get to it: no pick
		// -> stuck -> put-back loop); after the menu's "Come here" (fly true) the target he was on is dropped as before (the command shows),
		// the other attackers near the player stay pickable. Far ones and old hunts beyond it are ignored as before - and one of them that
		// comes within the radius while it attacks the player during the window has the recall's ignore lifted (Classify; the r3m attackers
		// were 2573 / 2241 / 428 u from him over its 3 samples) - never a give-up's own ignore (Mem::giveUpAt / preRecallIgn).
		constexpr double kRecallOwnPb = 2.0;
		const bool       keepPl = _p.recallKeepPlAtkR > 0;
		const bool       ownPb = a_now >= _lastPutBackAt - kTiny && a_now - _lastPutBackAt <= kRecallOwnPb + kTiny;
		const bool       dropOk = !ownPb && !a_fly;
		for (std::uint32_t id : _lastIds) {
			if (_p.recallKeepAttackers >= 0.5 && id != _target && std::find(_lastAtk.begin(), _lastAtk.end(), id) != _lastAtk.end()) continue;
			if (keepPl && (id != _target || dropOk) && std::find(_lastPlAtk.begin(), _lastPlAtk.end(), id) != _lastPlAtk.end()) {
				++_st.recallPlAtkKept;
				if (id == _target) ++_st.recallPlAtkDropKept;
				continue;
			}
			Mem& m = M(id);
			if (keepPl) {
				m.preRecallIgn = m.ignoreUntil;
				m.recallIgnAt = a_now;
			}
			m.ignoreUntil = std::max(m.ignoreUntil, a_now + _p.recallIgnore);
		}
		_recallDropId = _target;
		_recallDropOk = dropOk;
		NoteEnd("recall");
		DropTarget();
		_returnBlocked = false;  // ROUND3 SPEED (S4)
		_recallFly = a_fly;
		_returning = a_fly;
		_returnSince = a_fly ? a_now : -1;
		_noReturnUntil = a_fly ? -1e9 : a_now + _p.recallIgnore;  // hard stop: Papyrus places him, no flight back meanwhile
		_returnNoPathSince = -1;
		_returnCheckAt = -1e9;
		_retBest = 1e18;  // ROUND5: the return's progress clock starts with the flight back
		_retProgAt = a_now;
		_retEsc = 0;
		_pushFor = 0;  // a fresh push state for this flight back
		_returnStuck = false;  // ROUND5 A2
		_recallAt = a_now;     // HOTFIX round5C: the no-idle fallback keeps the recall (only live hitters, like Recall itself)
		++_st.recalls;
	}

	void Brain::SetVerdicts(std::span<const std::uint32_t> a_deny, std::span<const std::uint32_t> a_clear, double a_now, double a_playerHealthPct)
	{
		_deny.clear();
		for (std::uint32_t id : a_deny) _deny.insert(id);
		for (std::uint32_t id : a_clear)
			if (!_deny.contains(id)) M(id).clearedUntil = a_now + _p.clearTtl;
		// ROUND3 fix 2: the push gap (Papyrus passes slow down with the frame rate: 16 fps gave 45-49 % stale frames)
		if (_everVerdict) {
			const double gap = a_now - _lastVerdictAt;
			if (gap > 0 && gap < 30.0) _verdictGap = _verdictGap > 0 ? 0.7 * _verdictGap + 0.3 * gap : gap;
		}
		_everVerdict = true;
		_lastVerdictAt = a_now;
		if (a_playerHealthPct >= 0) _verdictHealth = std::clamp(a_playerHealthPct, 0.0, 1.0);
	}

	bool Brain::Cleared(std::uint32_t a_id, double a_now) const
	{
		const auto it = _mem.find(a_id);
		return it != _mem.end() && it->second.clearedUntil > a_now;
	}
	bool Brain::Ignored(std::uint32_t a_id, double a_now) const
	{
		const auto it = _mem.find(a_id);
		return it != _mem.end() && it->second.ignoreUntil > a_now;
	}
	bool Brain::Spent(std::uint32_t a_id, double a_now) const
	{
		const auto it = _mem.find(a_id);
		return it != _mem.end() && it->second.spentUntil > a_now;
	}
	const char* Brain::WhyNot(std::uint32_t a_id) const
	{
		const auto it = _mem.find(a_id);
		return it == _mem.end() ? "unknown" : it->second.why;
	}
	const char* Brain::BombWhyName(std::uint8_t a_why)  // finish A2 lane 1 r2 (SPN|PROT|bomberWhy)
	{
		switch (a_why) {
		case BombWhy::kSwap: return "swap";
		case BombWhy::kNotThrower: return "notThrower";
		case BombWhy::kNoProg: return "noProg";
		case BombWhy::kFarMe: return "farMe";
		case BombWhy::kNoAim: return "noAim";
		case BombWhy::kHidHold: return "hidHold";
		case BombWhy::kStall: return "stall";
		case BombWhy::kNone: return "none";
		default: return "other";
		}
	}
	bool Brain::LastSight(std::uint32_t a_id, double& a_at, bool& a_clear) const
	{
		const auto it = _mem.find(a_id);
		if (it == _mem.end() || it->second.vis < 0 || it->second.visAt < -1e8) return false;
		a_at = it->second.visAt;
		a_clear = it->second.vis == 1;
		return true;
	}

	void Brain::Prune(double a_now)
	{
		_lastPruneAt = a_now;
		for (auto it = _mem.begin(); it != _mem.end();) {
			const Mem& m = it->second;
			if (it->first != _target && m.seenAt < a_now - 30.0 && m.ignoreUntil < a_now && m.spentUntil < a_now && m.clearedUntil < a_now &&
				*std::max_element(std::begin(m.essAt), std::end(m.essAt)) < EssClock(a_now) - 60.0) {  // HOTFIX round5C: the essential knock-down history (D1H: its clock)
				// completion side lane 4 s13r2 (brain.newcomerKnown): a pruned candidate is known - back in the list, it is no newcomer
				if (_p.newcomerKnown >= 0.5) {
					if (_knownIds.size() >= 4096) _knownIds.clear();  // (bounded; a long session of spawned FF refs)
					_knownIds.insert(it->first);
				}
				it = _mem.erase(it);
			} else
				++it;
		}
	}

	void Brain::SetVis(Mem& a_m, bool a_v) const
	{
		// DPREP-T (G1, live 21:29:55-21:30:21: the hidden challenger's clock restarted on ONE visible round-robin ray, so it
		// had 0.2 s at full priority = 6.7x the hidden current target): with occFlickerKeep > 0 the hidden clock (occSince)
		// survives a visible run shorter than occFlickerKeep s, and is cleared only after that much continuous sight.
		const bool keep = _p.occFlickerKeep > 0;
		if (a_v) {
			if (a_m.vis != 1 || a_m.visRunAt < 0) a_m.visRunAt = _now;
			a_m.vis = 1;
			if (!keep || _now - a_m.visRunAt >= _p.occFlickerKeep - kTiny) a_m.occSince = -1;
			a_m.visTrueAt = _now;
		} else {
			if (keep ? a_m.occSince < 0 : (a_m.vis != 0 || a_m.occSince < 0)) a_m.occSince = _now;
			a_m.vis = 0;
			a_m.visRunAt = -1;
		}
		a_m.visAt = _now;
	}

	void Brain::GiveUpTarget(std::uint32_t a_id, double a_now, double a_forS, GiveUp a_why, Out& out)
	{
		Mem& m = M(a_id);
		m.giveUpAt = a_now;  // FINISH B2 fix lane 5: a give-up's ignore is never lifted by the recall keep
		// A failed landing is not a generic target ignore: it must release immediately if this foe attacks the player.
		const bool fromLanding = _p.blinkUndoneT > 0 && _p.blinkUndoneR > 0 && (a_why == GiveUp::kUnreach || a_why == GiveUp::kNoPath) &&
			((a_id == _target && _stage >= 4 && std::string_view(_stageWhy ? _stageWhy : "") == "undone") ||
			 std::abs(a_now - m.undoneRefusalAt) <= kTiny);
		if (fromLanding) {
			m.undoneUntil = std::max(m.undoneUntil, std::min(a_now + a_forS, _undoneAt + _p.blinkUndoneT));
			m.undoneAnchor = _undoneLand;
		} else {
			m.ignoreUntil = std::max(m.ignoreUntil, a_now + a_forS);
		}
		m.undoneRefusalAt = -1e9;
		m.blindT = -1;
		out.giveUp = a_why;
		out.giveUpId = a_id;
	}

	void Brain::RequestPutBack(PutBackWhy a_why, Out& out)
	{
		if (_now - _lastPutBackAt < _p.putBackGap) return;
		// D2 (user 03:52: 16 put-backs in 21 min, 03:41:13 / 03:42:30 / 03:43:17 'putback why=1' 2500-3200 u after far
		// fights, why=2 three times in 18 s while following; the goal: < 2 teleports per 10 min, all out of view): a stuck /
		// failed-return put-back is allowed only while the player cannot see him AND he is beyond d2PutBackFar u (or has
		// tried to get back for d2PutBackStuckMax s). Refused, the way back starts over (navmesh first, every other refusal
		// the sky / side plan), so the flight back goes on. d2PutBackSeenMax s of refusals: allowed anyway. Lost (why 3) is
		// never refused. brain.d2PutBack 0 = D1H.
		const Params& p = _p;
		// R6 step 10 ([R6] Tele 1, ARCH §4.11): the Pilot's policy. A lost transfer only while the Pilot's lost condition holds
		// (the player in another space for > 3 s, or > 8000 u away without progress for 30 s): in the SAME space (inside the
		// loaded area) the out-of-view return blink behind the player (blinkWhy 4; the runtime judges both ends again before the
		// move); no unseen spot = refused and tried again after extLostRetry s, never the Papyrus placement beside the player;
		// another space (or beyond the loaded area) = the Papyrus put-back 3 (behind the load screen). Every other put-back
		// (stuck, failed return) is never made: the way back starts over (navmesh, the sky / side plan, the push).
		if (_curF && TeleOn(*_curF)) {
			const Facts& f = *_curF;
			if (a_why == PutBackWhy::kLost) {
				++_st.teleLostAsks;
				if (!f.tele.mayLost) {
					++_st.telePbRefused;
					return;
				}
				if (!f.tele.otherSpace && _curW && GuardDist(f.meFeet, f.playerFeet) <= p.HuntAbort(f.uGrids)) {
					if (_now - _extLostTryAt < p.extLostRetry && _now >= _extLostTryAt) return;  // a refused try just now: no rays every frame
					_extLostTryAt = _now;
					_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
					Vec3 feet{};
					if (ReturnBlinkSpot(f, p, *_curW, feet)) {
						out.blink = true;
						out.blinkFeet = feet;
						out.blinkWhy = 4;
						out.target = 0;
						_lastPutBackAt = _lastBlinkAt = _now;
						++_st.extLostBlinks;
						_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
						_returnSince = _returnNoPathSince = -1;
						_stuck.Reset();
						_stuckFor = _stuckFollowFor = 0;
						_stuckSince = -1;
						return;
					}
					++_st.extLostNoSpot;
					++_st.telePbRefused;
					return;
				}
				// another space / beyond the loaded area: the Papyrus put-back 3 (below, the plain path)
				_pbSeenSince = -1e9;
				_lastPutBackAt = _now;
				out.putBack = true;
				out.putBackWhy = a_why;
				++_st.putBacks;
				_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
				_returnSince = _returnNoPathSince = -1;
				_stuck.Reset();
				_stuckFor = _stuckFollowFor = 0;
				_stuckSince = -1;
				return;
			}
			// stuck / failed return: never a put-back - he flies back (navmesh first on odd refusals, the sky / side plan or
			// the push on even ones), the stuck evidence restarts
			if (_now - _d2PbAt < 0.5 && _now >= _d2PbAt) return;  // refused just now
			_d2PbAt = _now;
			++_d2PbN;
			++_st.telePbRefused;
			_stuck.Reset();
			_stuckFor = _stuckFollowFor = 0;
			_stuckSince = -1;
			if (p.retEndWait > 0 && _retWait) return;  // FINISH B2 Fable (brain.retEndWait): waiting at the way back's off-mesh end - the loop is not restarted
			if (_curW) {
				const Vec3 spot = f.haveFollowSpot ? f.followSpot : f.playerFeet;
				const bool navOk = p.nav >= 0.5 && p.returnNav >= 0.5 && _curW->NavAvailable();
				const bool useNav = navOk && ((_d2PbN & 1) == 1 || f.interiorCell);
				StartBlockedReturn(Length(spot - f.meFeet), useNav, true, f.interiorCell ? 2 : 0);
			}
			return;
		}
		// R6 step 4 (Facts::ext.on, ARCH §4.11 lost transfer): only with the Warden's lost condition (his guard distance beyond
		// tele.lostDist for tele.lostSeconds) and his take-off out of the player's view; else he keeps flying back (the abort
		// branch hands him to the follow)
		// (a take-off in another space or beyond the loaded area - huntAbort - is never on the player's screen: not judged, P-R6-4)
		if (_curF && _curF->ext.on && a_why == PutBackWhy::kLost &&
			(!_curF->ext.mayLost ||
				(!_curF->ext.otherSpace && GuardDist(_curF->meFeet, _curF->playerFeet) <= p.HuntAbort(_curF->uGrids) && _curW && PlayerSees(*_curF, p, *_curW, _curF->meChest)))) {
			++_st.extPbRefused;
			return;
		}
		// step 4 fix lane 1 (REVIEW F1, ARCH §4.11 / INV-11: "the D2 return spot 420 u behind the player ... both ends out of
		// view"; the Papyrus put-back placed him 98 u BESIDE the player - on screen in third person - and also recalled the
		// brain, forgot the fight and sent him home): while the Warden acts, a lost transfer in the SAME space (the player's
		// space, inside the loaded area) is a native blink to ReturnBlinkSpot (floor, |dz| <= 200, a clear line from the
		// player's chest, the D2 eye test unseen); the runtime's DoBlink judges BOTH ends again with the eye test and the real
		// camera before the move (JumpViewOk "lost"). No unseen spot: refused, tried again after extLostRetry s - never the
		// Papyrus placement beside the player. Another space (or beyond the loaded area) keeps the Papyrus put-back (3): a
		// controller warp cannot cross cells.
		if (_curF && _curF->ext.on && a_why == PutBackWhy::kLost && p.extLostBlink >= 0.5 && !_curF->ext.otherSpace && _curW &&
			GuardDist(_curF->meFeet, _curF->playerFeet) <= p.HuntAbort(_curF->uGrids)) {
			if (_now - _extLostTryAt < p.extLostRetry && _now >= _extLostTryAt) return;  // a refused try just now: no rays every frame
			_extLostTryAt = _now;
			const Facts& f = *_curF;
			_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
			Vec3 feet{};
			if (ReturnBlinkSpot(f, p, *_curW, feet)) {
				out.blink = true;
				out.blinkFeet = feet;
				out.blinkWhy = 4;
				out.target = 0;
				_lastPutBackAt = _lastBlinkAt = _now;
				++_st.extLostBlinks;
				_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
				_returnSince = _returnNoPathSince = -1;
				_stuck.Reset();
				_stuckFor = _stuckFollowFor = 0;
				_stuckSince = -1;
				return;
			}
			++_st.extLostNoSpot;
			++_st.extPbRefused;
			return;
		}
		// R6 step 4 (Facts::ext.on, ARCH §4.11 on the old mover): a stuck / failed-return put-back is never made - the way back
		// restarts exactly like a D2 refusal (navmesh, then the sky / side plan, then the push); only the lost transfer remains
		const bool extNoPb = _curF && _curF->ext.on && a_why != PutBackWhy::kLost;
		if ((p.d2PutBack >= 0.5 || extNoPb) && a_why != PutBackWhy::kLost && _curF && _curW) {
			const Facts& f = *_curF;
			if (_now - _d2PbAt < 0.5) return;  // refused just now: no view ray every frame
			if (_d2PbSince < -1e8) _d2PbSince = _now;
			const double waited = _now - _d2PbSince;
			const double dP = Length(f.playerFeet - f.meFeet);
			const bool   seen = !extNoPb && PlayerSees(f, p, *_curW, f.meChest);
			const bool   allow = !extNoPb && ((!seen && (dP > p.d2PutBackFar || waited >= p.d2PutBackStuckMax - kTiny)) || waited >= p.d2PutBackSeenMax - kTiny);
			if (!allow) {
				_d2PbAt = _now;
				++_d2PbN;
				++_st.d2PbRefused;
				if (extNoPb) ++_st.extPbRefused;
				_stuck.Reset();
				_stuckFor = _stuckFollowFor = 0;
				_stuckSince = -1;
				// FINISH B2 Fable (brain.retEndWait): he waits at the way back's off-mesh navmesh end (ReturnMove) - a refused put-back no longer
				// restarts nav -> push -> sidestep (the finA2 soaks' loop: 'pbRef' 3 -> 6 in 7 s while he flew the ArcJet upper floor); the
				// refusal clock (d2PutBackSeenMax) runs on, so the allowed put-back comes when it did before
				if (p.retEndWait > 0 && _retWait) return;
				const Vec3 spot = f.haveFollowSpot ? f.followSpot : f.playerFeet;
				const bool navOk = p.nav >= 0.5 && p.returnNav >= 0.5 && _curW->NavAvailable();
				// even refusals: the navmesh leg (or the push without it); odd ones: the sky / side plan in an exterior
				const bool useNav = navOk && ((_d2PbN & 1) == 1 || f.interiorCell);
				StartBlockedReturn(Length(spot - f.meFeet), useNav, true, f.interiorCell ? 2 : 0);
				return;
			}
			++_st.d2PbAllowed;
			// D2 (brain.d2ReturnBlink): the put-back as a native blink to a spot behind the player, out of view (the Papyrus
			// placement puts him beside the player, in sight)
			if (p.d2ReturnBlink >= 0.5 && Length(f.playerFeet - f.meFeet) <= p.d2ReturnBlinkMax) {  // (farther: the Papyrus placement)
				_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
				Vec3 feet{};
				if (ReturnBlinkSpot(f, p, *_curW, feet)) {
					out.blink = true;
					out.blinkFeet = feet;
					out.blinkWhy = 4;
					out.target = 0;
					_lastPutBackAt = _lastBlinkAt = _now;
					++_st.d2ReturnBlinks;
					_d2PbSince = -1e9;
					_d2PbN = 0;
					_pbSeenSince = -1e9;
					_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
					_returnSince = _returnNoPathSince = -1;
					_stuck.Reset();
					_stuckFor = _stuckFollowFor = 0;
					_stuckSince = -1;
					return;
				}
			}
			_d2PbSince = -1e9;
			_d2PbN = 0;
			_pbSeenSince = -1e9;
			_lastPutBackAt = _now;
			out.putBack = true;
			out.putBackWhy = a_why;
			++_st.putBacks;
			_returning = _recallFly = _returnBlocked = false;
			_returnStuck = false;
			_returnSince = _returnNoPathSince = -1;
			if (p.putBackResetStuck >= 0.5) {
				_stuck.Reset();
				_stuckFor = _stuckFollowFor = 0;
				_stuckSince = -1;
				++_st.stuckResets;
			}
			return;
		}
		// DPREP-FOLLOW (user 21:22: "remove the stuck put-back teleport while following, or make it invisible: only when out
		// of the player's view"; live 8 of 9 put-backs came while following; GATE_B fail 4 Concord why=2): with no target, a
		// stuck / failed-return put-back the player would see is refused: the stuck evidence restarts and the return flies
		// the navmesh back with a fresh plan (re-anchor + re-plan). After followPutBackSeenMax s of refusals it is allowed
		// (never stuck for good). Lost (why 3: far away, fast travel) is never refused. brain.followPutBackUnseen 0 = round 5 B.
		if (_p.followPutBackUnseen >= 0.5 && _target == 0 && a_why != PutBackWhy::kLost && _curF && _curW) {
			if (_now - _pbRefusedAt < 0.5) return;  // refused just now: no view ray every frame
			const bool capped = _pbSeenSince > -1e8 && _now - _pbSeenSince > _p.followPutBackSeenMax + kTiny;
			if (!capped && PlayerSees(*_curF, _p, *_curW, _curF->meChest)) {
				if (_pbSeenSince < -1e8) _pbSeenSince = _now;
				_pbRefusedAt = _now;
				++_st.putBacksSeenRefused;
				_stuck.Reset();
				_stuckFor = _stuckFollowFor = 0;
				_stuckSince = -1;
				if (p.retEndWait > 0 && _retWait) return;  // FINISH B2 Fable (brain.retEndWait)
				const Facts& f = *_curF;
				const Vec3   spot = f.haveFollowSpot ? f.followSpot : f.playerFeet;
				const bool   navOk = _p.nav >= 0.5 && _p.returnNav >= 0.5 && _curW->NavAvailable();
				StartBlockedReturn(Length(spot - f.meFeet), navOk, true, 2);
				return;
			}
		}
		_pbSeenSince = -1e9;
		_lastPutBackAt = _now;
		out.putBack = true;
		out.putBackWhy = a_why;
		++_st.putBacks;
		_returning = _recallFly = _returnBlocked = false;
		_returnStuck = false;  // ROUND5 A2
		_returnSince = _returnNoPathSince = -1;
		// ROUND4 fix 2 (in game R1: "brain putback why=2" 3x in 1.4 s, stuckT 3.42 right after the first): the put-back moves
		// him, so the stuck evidence is from the old place. Rollback: brain.putBackResetStuck = 0.
		if (_p.putBackResetStuck >= 0.5) {
			_stuck.Reset();
			_stuckFor = _stuckFollowFor = 0;
			_stuckSince = -1;
			++_st.stuckResets;
		}
	}

	// ------------------------------------------------------------------------------------------- tiers (ROUND2 §3.1)
	Tier Brain::Classify(const Cand& c, const Ctx& cx, Mem& m, Info& in) const
	{
		const Params& p = _p;
		in = Info{};
		in.m = &m;
		in.dCentre = GuardDist(c.feet, cx.centre);
		in.dPlayer = GuardDist(c.feet, cx.playerFeet);
		const double now = cx.now;
		auto no = [&](const char* a_why) {
			m.why = a_why;
			in.tier = Tier::kNone;
			return Tier::kNone;
		};
		if (!c.hostile) return no("notHostile");
		if (!c.loaded) return no("unloaded");
		if (!c.sameSpace) return no("otherSpace");
		if (c.killPending) return no("killPending");
		if (!c.highProcess && p.midHighTargets < 0.5) return no("midHigh");
		const double hitAny = now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt });
		if (c.denied || _deny.contains(c.id)) {
			// HOTFIX round5C A: the Papyrus vetting denies scene actors out of combat and keeps its deny list for a while; a
			// foe that hit the player / him / a teammate within attackerValid is fought anyway (every native hard veto -
			// not hostile, settler, companion, teammate, commanded, child, down - still applies). brain.defendOverDeny 0: off.
			// s11 fix lane 3 (brain.provokedFight; the s4f P05 cause: a dormant Swan the player shot was vetted 'not hostile' at the
			// shot and that deny held it 0.2-0.5 s after it turned on the player): a deny judged before the foe was hostile / in
			// combat is stale once it attacks the player (combat target = the player or a hit on it) or is the player's fight -
			// every native hard veto (companion, settler, teammate, child, down ...) is in c.hostile and still applies
			// (the player's fight only inside the local ring: a far foe the player snipes waits for the game to turn it - no far hunt)
			const bool fightOver = p.provokedFight >= 0.5 && c.hostile && (c.attackingPlayer || (c.playerFight && in.dCentre <= std::max(p.rangedGuard, cx.R)));
			if (!(p.defendOverDeny >= 0.5 && c.defend && hitAny <= p.attackerValid) && !fightOver) return no("deny");
			in.denyOver = true;  // counted in Update (Classify is const)
		}
		if (c.nativeDeny) return no("nativeDeny");

		in.undoneHold = p.blinkUndoneT > 0 && p.blinkUndoneR > 0 && m.undoneUntil > now &&
			!BlinkProtectsPlayer(c, p, now) && RefusedBlinkLanding(c.feet, &m.undoneAnchor, p.blinkUndoneR);
		const bool   ignored = m.ignoreUntil > now || in.undoneHold, spent = m.spentUntil > now;
		const double hitPlayer = now - c.lastHitPlayerAt, hitHim = now - c.lastHitHimAt;
		const bool   provoked = c.angryWithPlayer || c.quarrel;
		// ROUND4 (in game R3, Sanctuary house: at the doorway the foe was dropped as "hidden" once its attacker window
		// lapsed, then a return and a visible put-back): the current target that the reach chain is working on (stage 0-3)
		// within reachCommitR stays a target while hidden, with the Papyrus token it had (cleared within navHiddenMax s).
		// Every hard veto above still applies (deny, hostility, nativeDeny); the hidden caps still end it.
		// ROUND4 fix 2 (in game R1, museum upstairs: the door AI walked him to 1697 u and the target was dropped at once,
		// "hidden:772", then 8.4 s standing still): the radius also holds from where the chain started, up to reachCommitMax u
		const double dCommit = Length(c.chest - cx.meChest);
		in.committed = c.id == _target && _stage >= 0 && _stage <= 3 && p.reachCommitR > 0 &&
		               (dCommit <= p.reachCommitR || (_chainD0 <= p.reachCommitR && dCommit <= std::max(p.reachCommitR, p.reachCommitMax))) &&
		               (c.cleared || m.clearedUntil > now - p.navHiddenMax || c.combatWithGroup || hitAny <= p.hitMemory);
		// ROUND5 B (gate A2 defect 2, c120-r5 Sanctuary house: right after a reach blink the raider was seen, the chain stage
		// went back to -1, and 0.2 s later 'hidden:414' dropped it for good - never killed): the current target seen within
		// commitSeen s and within reachCommitR stays committed while it is hidden (the reach chain takes it). 0 = round 5 A.
		if (!in.committed && p.commitSeen > 0 && c.id == _target && now - m.visTrueAt <= p.commitSeen && dCommit <= p.reachCommitR &&
			(c.cleared || m.clearedUntil > now - p.navHiddenMax || c.combatWithGroup || hitAny <= p.hitMemory))
			in.committed = true;
		// D2 tuner (GATE_D1H1 fail 4, d1h-r4m120 15:46:27.149: his own reach blink, then 'HUNT|return' and tgt=0 for 1.45 s with
		// the raider kept 265 u away: the hidden first-strike target lost its chain stage (-1 after the blink) and with it the
		// commitment). d2BlinkCommit 1: the target his own blink went to stays committed for blinkKeepFor s after it.
		if (!in.committed && p.d2BlinkCommit >= 0.5 && c.id == _target && c.id == _blinkKeepId && now < _blinkKeepUntil) in.committed = true;
		const bool   token = c.cleared || m.clearedUntil > now || (in.committed && m.clearedUntil > now - p.navHiddenMax);
		// DPREP-T (live 21:38:24-26: 'HUNT|start 0011AE50 ... end why=drop' 3x in 1.6 s): a first-strike foe dropped for being
		// hidden (K3, fpHiddenDrop) is "hidden" again until it has been seen fpRepickSeen s without a break (fpRepickFor s)
		const bool fpRecent = p.fpHiddenDrop > 0 && c.id != _target && now - m.fpDropAt <= p.fpRepickFor + kTiny &&
		                      !(m.vis == 1 && m.visRunAt >= 0 && now - m.visRunAt >= p.fpRepickSeen - kTiny);
		// ... and the CURRENT one is "hidden" (K3) only when not seen for fpHiddenDrop s (the pick loop has the same rule)
		const bool hiddenNow = m.vis == 0 && now - m.visAt <= 2.0 * p.visAge;
		const bool fpHold = p.fpHiddenDrop > 0 && c.id == _target && now - m.visTrueAt < p.fpHiddenDrop - kTiny;
		// step-2 fixer (s2fBigFirst; Concord r3m 21:40: the quest deathclaw came at the player while he chased raiders behind
		// the ruins - hidden from HIM, not yet in combat, so no tier at all until 8.7 s): a big hostile within d2DbBigR of
		// the PLAYER is never dropped as "hidden" and needs no Papyrus token (the reach chain takes it)
		const bool bigNearPlayer = p.s2fBigFirst >= 0.5 && c.cls == Cls::kBig && !c.essential && in.dPlayer <= p.d2DbBigR;
		auto       hiddenGate = [&]() {
			if (bigNearPlayer) return false;
			if (!(((hiddenNow && !fpHold) || fpRecent) && !in.committed && !InteriorHuntOk(c, cx))) return false;
			if (p.fpHiddenDrop > 0 && c.id == _target && hiddenNow) {  // DPREP-T: the current one is dropped hidden now
				m.fpDropAt = now;
				++_fpDropN;
			}
			return true;
		};
		// worth a round-robin ray: inside the largest reach that can apply now (Near: the local rings only)
		const double sightR = cx.range >= 1 && !cx.waiting ? cx.maxR : std::max({ p.attackerGuard, p.rangedGuard, p.waitHuntRange, cx.R });
		in.sightful = in.dCentre <= sightR + p.leashExtra;

		auto accept = [&](Tier a_t, double a_reach) {
			in.tier = a_t;
			in.reach = a_reach;
			in.pickable = !ignored && !spent;  // an ignored / spent one keeps its tier for logs; Pick skips it
			m.why = ignored ? "ignored" : (spent ? "spent" : "");
			// s13 fix r1 lane 2 (brain.essHoldStay): the held essential he just control-hit stays his target through its window (no
			// new hit: EssStaying); one that shoots the player again after that hit (not the shot already flying) ends it at once
			if (spent && !ignored && p.essHoldStay >= 0.5 && c.id == _target && c.essentialOnly && !c.killPending && m.essStayUntil > now) {
				if (c.lastHitPlayerAt > m.essStayAt + kEssDownUpHit) {
					m.essStayUntil = now;
					m.spentUntil = std::min(m.spentUntil, now);
					m.why = "";
					in.essStayShot = true;  // (counted in Update: Classify is const)
				} else {
					in.essStay = true;
					m.why = "essStay";
				}
				in.pickable = true;
			}
			return a_t;
		};
		auto pend = [&](Tier a_t, const char* a_why) {
			in.pending = !ignored && !spent;
			in.pendTier = a_t;
			return no(a_why);
		};

		// L-Attacker: hit the group within attackerValid (WAIT: only hits on him)
		const double atk = cx.waiting ? hitHim : hitAny;
		if (atk <= p.attackerValid && in.dCentre <= p.attackerGuard) {
			if (provoked && !(p.defendProvoked >= 0.5 && hitPlayer <= p.attackerValid)) return no(c.quarrel ? "quarrel" : "angryWithPlayer");
			in.pin = !cx.waiting && hitPlayer <= p.attackerValid &&
			         (cx.health < p.lowHealth || GuardDist(c.feet, cx.playerFeet) < p.threat) && in.dCentre <= p.pinRange;
			return accept(Tier::kAttacker, p.attackerGuard);
		}
		if (provoked) return no(c.quarrel ? "quarrel" : "angryWithPlayer");
		if (cx.dialogue) return no("dialogue");  // K14: only attackers are answered during a conversation

		const double localReach = cx.waiting ? p.waitHuntRange : (c.cls != Cls::kRegular ? std::max(p.rangedGuard, cx.R) : cx.R);
		// ROUND3 fix 2 (in game R2, a house attacker at 272 u dropped as "stale" once its 3 s attacker window ended): a foe
		// that hit the group within hitMemory still fights; it stays Engaged (no token, no stale gate). Rollback:
		// brain.staleHitEngaged = 0.
		const bool hitRecent = p.staleHitEngaged >= 0.5 && hitAny <= p.hitMemory;
		// ROUND3 fix 2 (Sully at 1749 u refused for 20 s as "stale"): stale verdicts mean the Papyrus VM is slow, not that
		// the foe is unfit. Since the first push of the session, a first strike on a non-essential hostile goes on the
		// native checks (every hard veto, the last deny set, sight, sneaking / busy / requireCombat). Rollback:
		// brain.staleNative = 0.
		const bool nativeOk = cx.staleNative && !c.essential;
		if (in.dCentre <= localReach) {
			// s11 fix lane 3 (brain.provokedFight): the player's fight is Engaged at the shot (no Papyrus token: the vetting would
			// call it 'not hostile' until the engine turns it on the player 0.5-0.6 s later)
			if (c.combatWithGroup || hitRecent || (p.provokedFight >= 0.5 && c.playerFight && c.hostile)) return accept(Tier::kEngaged, localReach);
			// L-Preempt (K3): inside the ring only, seen, a Papyrus token, the player not sneaking / busy
			if (p.requireCombat >= 0.5) return no("requireCombat");
			// R6 step 3 fix lane 1 (sc20 S3_CRIME, gate run 072325: the idle Diamond City guard FF0010C3, crime=true, not in
			// combat, picked as tier P 0.25 s after its token and killed): no first strike on an idle crime-faction actor
			if (p.crimeNoFirst >= 0.5 && c.crimeFaction && !c.inCombat) return no("crimeFirst");
			if (cx.stale && !nativeOk) return no("stale");
			if (cx.sneaking) return no("sneaking");
			if (cx.busy) return no("busy");
			if (hiddenGate()) {  // ROUND4: not the committed target; ROUND5: nor his interior; DPREP-T: fpHold / fpRecent
				in.preVet = !ignored && !spent && !token;  // ROUND3 fix 5: ask Papyrus now, so the token is there when he sees it
				return no("hidden");
			}
			// completion side lane 4 s13r2 (brain.preemptAimKeep; REVIEW_r3 4): the player aimed at this foe with a drawn weapon within
			// preemptAimKeep s - it may be pacified (Intimidation / Animal Friend / Wasteland Whisperer: only Brain.psc Untouchable reads
			// IsIntimidated) - so no no-token first strike (BigSeen, bigNearPlayer, stale-native, NearSeen): the Papyrus vetting decides
			if (!token && AimKept(m, cx.now)) {
				++_preAimKeepN;
				return pend(Tier::kPreempt, "token");
			}
			if (!token && !BigSeen(c, cx, m) && !bigNearPlayer && !(cx.stale && nativeOk)) {
				if (!NearSeen(c, cx, m)) return pend(Tier::kPreempt, "token");
				++_preNoTokN;  // s12 fix r3 lane 3 (brain.preemptNoTokenR)
			}
			return accept(Tier::kPreempt, localReach);
		}

		// far tiers (F1-F3, FP)
		if (cx.range < 1) return no("range");
		// HOTFIX round5C D (03:38:19-23 'chainRest' while foes shot the player): the soft far gates (chain rest, stale
		// verdicts, player busy / sneaking) never hold back a foe attacking the player. brain.defendFarOpen 0: round 5 C.
		if (!cx.farOpen && (cx.farHard || !(p.defendFarOpen >= 0.5 && c.attackingPlayer))) return no(cx.farWhy);
		// ROUND3 (live 1706, Concord): every raider and the deathclaw carried a quest alias, so K21 made him ignore them.
		// V07 (hostile to the player now) is hard and V09 / V11 keep provoked neutrals out, so essential / unique / alias /
		// crime are facts, not limits (the engine still spares an essential: runtime NativeDamage MayKill). Rollback:
		// [Tabs] brain.protectedTiers = 1.
		if (p.protectedTiers >= 0.5) {
			if (c.essential) return no("essential");
			if (c.unique) return no("unique");
			if (c.questAlias) return no("questAlias");
			if (c.crimeFaction) return no("crimeFaction");
		}
		if (c.underwater) return no("underwater");
		const double mem = p.hitMemory;
		const bool   engaged = c.combatWithGroup || (c.id == _target && _tFar && now - m.engagedAt <= p.disengageGrace);
		Tier         t = Tier::kNone;
		double       reach = 0;
		if (hitPlayer <= mem) {
			t = Tier::kFar1;
			in.sub = 0;
		} else if (now - c.lastHitGroupAt <= mem && (hitHim > mem || c.lastHitGroupAt > c.lastHitHimAt + 1e-6)) {
			t = Tier::kFar1;
			in.sub = 1;
		} else if (hitHim <= mem) {
			t = Tier::kFar1;
			in.sub = 2;
		} else if (engaged && c.combatWithPlayer && c.weaponDrawn) {
			t = Tier::kFar2;
		} else if (engaged) {
			t = Tier::kFar3;
		}
		if (t == Tier::kNone) {
			if (p.crimeNoFirst >= 0.5 && c.crimeFaction && !c.inCombat) return no("crimeFirst");  // R6 step 3 fix lane 1 (sc20)
			// ROUND3 FP: hostile, not fighting the group yet, inside the Hunt mode's first-strike reach (sight + token, K3/K4)
			const PreemptReach pr = PreemptReaches(cx.range, p.rangedGuard, p.preemptFar, p.preemptFarBusy, p.preemptAnywhere, p.preemptAnywhereBusy,
				cx.farR, cx.maxR);
			const double fpReach = c.inCombat ? std::max(pr.idle, pr.busy) : pr.idle;
			if (cx.stale && p.requireCombat < 0.5 && !nativeOk) return no("stale");  // ROUND3 fix 2: or the native checks while the VM is slow
			if (!cx.preemptOpen) return no(p.requireCombat >= 0.5 ? "requireCombat" : "notEngaged");
			const bool pre = !ignored && !spent && !token;  // ROUND3 fix 5: worth asking Papyrus ahead of sight / reach
			if (in.dCentre > fpReach) {
				in.preVet = pre && in.dCentre <= fpReach * p.preVetPad && cx.health >= p.lowHealth;
				return no("notEngaged");
			}
			if (cx.health < p.lowHealth) return no("playerHurt");
			// s12 r2 fix lane 1 (s12r1 soak death K3 dt3 04:47:51.15-48:01: 1.1 s after the player arrived he took the street raider
			// 0018DEF8 3784 u away as a far first strike; at 04:47:56.7 the Runner's 10-raider ambush came at the player, him 6148 u
			// away, and the player died 4 s later): no first strike beyond fpPlayerR of the player (the current one within 1.25 x,
			// no flicker at the edge), none within fpArriveT s of the player's arrival somewhere new (Update: _plArriveAt)
			if (p.fpPlayerR > 0 && in.dPlayer > (c.id == _target ? 1.25 : 1.0) * p.fpPlayerR + kTiny) {
				// FINISH A2 fix lane 2 r2 (brain.fpHereR; gate finA K03, soak GK5 20261026-124632 12:49:32.35 'WARDEN|idle why=
				// 000DED99:fpFar,000DED97:fpFar,000DEDB1:fpFar' with him on ArcJet's turret platform 300-800 u from them, the player
				// touring 2500+ u away: he flew down, and 9-35 s later took them from BELOW through the shut way - 7 of the soak's 13
				// in1 stuck episodes; the installed build (no fpPlayerR) finished the visible ones up there in 0.3-0.5 s each): a foe
				// he SEES within fpHereR of himself is not a far trip - while nobody attacks the player he finishes the group he is
				// at. A hidden one, or any one while the player is attacked, stays refused as before.
				const bool nearSeen = cx.fpHereOn && m.vis == 1 && now - m.visTrueAt <= std::max(p.visAge, p.farVisAge) + kTiny &&
				                      Length(c.chest - cx.meChest) <= p.fpHereR + kTiny;
				if (!(nearSeen && cx.plQuiet)) {
					// (seen near him: the Papyrus vetting is asked ahead, so the token is there the moment nobody attacks the player)
					if (nearSeen) in.preVet = !ignored && !spent && !token;
					++_fpFarN;
					return no("fpFar");
				}
				++_fpHereN;
			}
			if (p.fpArriveT > 0 && now - _plArriveAt < p.fpArriveT - kTiny) {
				++_fpArriveN;
				return no("fpArrive");
			}
			if (hiddenGate()) {  // ROUND4: not the committed target; ROUND5: nor his interior; DPREP-T: fpHold / fpRecent
				in.preVet = pre;
				return no("hidden");
			}
			if (!token && AimKept(m, cx.now)) {  // completion side lane 4 s13r2 (brain.preemptAimKeep): Papyrus vets an aimed-at foe first
				++_preAimKeepN;
				return pend(Tier::kFarP, "token");
			}
			if (!token && !BigSeen(c, cx, m) && !(cx.stale && nativeOk)) return pend(Tier::kFarP, "token");
			return accept(Tier::kFarP, fpReach);
		}
		reach = t == Tier::kFar1 ? cx.maxR : (cx.range >= 2 ? cx.maxR : cx.farR);
		// ROUND3 fix (review, live 1706: player health 0.23 all session, FO4 does not regenerate): F2 / F3 foes already
		// fight the group, so a hurt player is a reason to go, not to stay. Only FP (an idle first strike) keeps the
		// playerHurt gate. Rollback: brain.hurtGateFar = 1 (round-2: F2 / F3 refused below lowHealth).
		if (p.hurtGateFar >= 0.5 && t != Tier::kFar1 && cx.health < p.lowHealth) return no("playerHurt");
		if (in.dCentre > reach) return no("far");
		// ROUND3: a far foe that fights / hit the group needs no Papyrus token (the local Engaged / Attacker tiers never did;
		// hostility, settler, companion, teammate, scene, speaker, angry and quarrel are native hard vetoes, and a Papyrus
		// deny still removes it). FP and Preempt keep the token. Rollback: brain.farTokenAll = 1.
		const bool fighting = c.combatWithGroup || hitAny <= mem;
		// ROUND3 fix 4 (in game R1, MQ102 alias bloatfly: "HUNT|end why=stale" 2.0 s into an F2 hunt while d fell 4278 -> 685,
		// then 0 of 6 killed in 60 s; the Papyrus VM was starved at low fps): stale verdicts close the far tiers only for a
		// foe that does not fight the group. A fighting foe needs no token, the deny set is kept, and every native hard veto
		// still applies. Rollback: brain.staleFightOpen = 0 (stale closes every far tier, round 3).
		if (cx.stale && (!fighting || p.staleFightOpen < 0.5)) return no("stale");
		if (!token && (p.farTokenAll >= 0.5 || !fighting)) return pend(t, "token");
		return accept(t, reach);
	}

	// ROUND3 fix 5 (in game R1: the idle deathclaw was picked 0.9 s after brain-on, Sully 1.0 s, waiting for the Papyrus
	// token round trip): a big hostile creature (Cls::kBig, not essential) that he sees within preemptNoTokenBig u needs no
	// token. Every native hard veto (not hostile, settler, companion, commanded, scene, speaker, down, ghost, mount, angry,
	// quarrel), the deny set, sneaking / busy / requireCombat / stale all still apply. 0 = off.
	bool Brain::BigSeen(const Cand& c, const Ctx& cx, const Mem& m) const
	{
		const Params& p = _p;
		if (!(p.preemptNoTokenBig > 0) || c.cls != Cls::kBig || c.essential) return false;
		if (m.vis != 1 || cx.now - m.visAt > 2.0 * p.visAge + kTiny) return false;
		return Length(c.chest - cx.meChest) <= p.preemptNoTokenBig;
	}
	// s12 fix r3 lane 3 (brain.preemptNoTokenR; s12r2 gate FEEL S6 0.26-0.35 s in 2 of 5 runs, 133019 trial 2 13:32:26.79-27.41:
	// the raider placed 10 m behind him was hostile but not in combat yet at the release - tier P - and 'WARDEN|idle ... :token'
	// waited 0.62 s for the Papyrus vetting round trip; in every fast trial the engine had already put it in combat, tier E, no
	// token): a SEEN plain hostile (not essential, unique, quest alias or crime faction - those keep the Papyrus vetting) within
	// preemptNoTokenR of him needs no token for the local first strike, like preemptNoTokenBig for big ones. The Papyrus pass
	// (Brain.psc Untouchable) only repeats what the native hard vetoes already refuse (down / bleedout, scene while not in combat,
	// commanded, settler, not hostile, the dialogue speaker); the deny set, sneaking / busy / requireCombat / crimeNoFirst / stale
	// still apply above. 0 = off (the s12r2 brain).
	bool Brain::NearSeen(const Cand& c, const Ctx& cx, const Mem& m) const
	{
		const Params& p = _p;
		if (!(p.preemptNoTokenR > 0) || !c.hostile || c.essential || c.unique || c.questAlias || c.crimeFaction) return false;
		if (m.vis != 1 || cx.now - m.visAt > 2.0 * p.visAge + kTiny) return false;
		return Length(c.chest - cx.meChest) <= p.preemptNoTokenR + kTiny;
	}
	// completion side lane 4 s13r2 (brain.preemptAimKeep; REVIEW_r3 4): the player aimed at it (Update stamps Mem::plAimAt while the
	// player's weapon is drawn and its chest is on the view ray) within preemptAimKeep s. 0 = off.
	bool Brain::AimKept(const Mem& a_m, double a_now) const
	{
		const double keep = _p.preemptAimKeep;
		return keep > 0 && a_m.plAimAt > -1e8 && a_now >= a_m.plAimAt - kTiny && a_now - a_m.plAimAt <= keep + kTiny;
	}

	// ------------------------------------------------------------------------------------------- HOTFIX round5C D: no idle
	// The fallback when the pick loop found nothing: the kept foes ATTACKING THE PLAYER (hit the player within
	// brain.defendWindow, or the player is their combat target) compete by the L4D2 score; ignored / hidden / given-up
	// states do not stop them. Never: not hostile, unloaded, other space, kill-pending, spent (flying from his punch),
	// native / Papyrus deny (unless the defend rule overrode the deny), provoked neutrals, middle-high (unless lifted),
	// resting / capped essentials, beyond the loaded-area reach (Near / wait / mounted: the attacker ring). The current
	// fallback target is kept until noIdleHold s pass without a hit, then another one gets a turn (rotation, no ping-pong).
	int Brain::NoIdlePick(const Facts& f, const Params& p, const Ctx& cx)
	{
		const auto& cs = f.cands;
		const int   n = static_cast<int>(cs.size());
		if (f.waiting || n == 0) return -1;
		const double now = cx.now;
		const double ring = std::max(p.attackerGuard, cx.R);
		const double reach = cx.farHard ? ring : std::max(ring, cx.maxR);
		auto ok = [&](int i) {
			const Cand& c = cs[i];
			const Info& in = _info[i];
			// D2 (goal 4: "never tgt=0 while kept foes exist and any is fighting the player or him"): with d2NoIdleFight a
			// foe whose combat target is the group, or that hit him / the group within attackerValid s, counts too
			const bool fights = c.attackingPlayer ||
			                    (p.d2NoIdleFight >= 0.5 && (c.combatWithGroup || now - std::max(c.lastHitHimAt, c.lastHitGroupAt) <= p.attackerValid));
			if (!fights || !in.m || in.essOut || in.farOut) return false;
			// GiveUpTarget can create this hold later than Classify in this same frame; read live memory here.
			if (p.blinkUndoneT > 0 && p.blinkUndoneR > 0 && in.m->undoneUntil > now &&
				!BlinkProtectsPlayer(c, p, now) && RefusedBlinkLanding(c.feet, &in.m->undoneAnchor, p.blinkUndoneR)) return false;  // s11 fix lane 2: nor a far hunt held back (farHuntPlayerR)
			if (!c.hostile || !c.loaded || !c.sameSpace || c.killPending || c.nativeDeny || c.angryWithPlayer || c.quarrel) return false;
			if (!c.highProcess && p.midHighTargets < 0.5) return false;
			if ((c.denied || _deny.contains(c.id)) && !in.denyOver) return false;
			if (in.m->spentUntil > now) return false;
			if (c.underwater && in.dCentre > ring) return false;
			// a recall (whistle, "come here", put-back) keeps its word for recallIgnore s: only foes that hit the group within
			// attackerValid (Recall's own live attackers) may take him back meanwhile
			// (FINISH B2 fix lane 5, brain.recallKeepPlAtkR: and foes attacking the player within recallKeepPlAtkR u of it - Recall keeps them
			// pickable for the same reason: the r5 / r3m idle stretches after the controller's teleport recall)
			// (... never the dropped target the keep does not keep: after his own put-back request or Come here)
			if (now - _recallAt < p.recallIgnore && now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt }) > p.attackerValid &&
				!(p.recallKeepPlAtkR > 0 && c.attackingPlayer && in.dPlayer <= p.recallKeepPlAtkR + kTiny && (c.id != _recallDropId || _recallDropOk)))
				return false;
			return in.dCentre <= reach;
		};
		auto score = [&](int i) {
			const Info& in = _info[i];
			const bool  occ = in.m->vis == 0;
			return Score(cs[i].chest, f.meChest, f.meVel, Priority(cs[i].cls, false, in.dPlayer, occ, p), p);
		};
		int    best = -1, alt = -1, held = -1;
		double bestS = -1e300, altS = -1e300;
		for (int i = 0; i < n; ++i) {
			if (!ok(i)) continue;
			const double s = score(i);
			if (cs[i].id == _fbId && cs[i].id == _target) held = i;
			if (_info[i].m->fbSkipUntil > now) {
				if (s > altS) {
					altS = s;
					alt = i;
				}
			} else if (s > bestS) {
				bestS = s;
				best = i;
			}
		}
		int sel = (held >= 0 && _info[held].m->fbSkipUntil <= now) ? held : (best >= 0 ? best : alt);
		if (sel < 0) {
			_fbId = 0;
			return -1;
		}
		// B2 codex review (BS-1, brain.noIdleUrgent): the protection order the normal pick has (ProtectFilter) never ran here - a
		// suicider 600 u from the player lost to a gunner 900 u away on score, and the hold kept the gunner noIdleHold s. The
		// urgent fallback foe (suicider > turned > thrower, each attacking the player) comes first, nearest the player, the
		// held one on a tie (no ping-pong); no rotation away from it
		int urgent = -1;
		if (p.noIdleUrgent >= 0.5) {
			const double suicR = p.protectSuicideR > 0 ? p.protectSuicideR : 3000.0;
			auto rank = [&](int i) {
				const Cand&  c = cs[i];
				const double dP = _info[i].dPlayer;
				if (c.suicider && (c.inCombat || c.attackingPlayer || c.combatWithGroup) && dP <= suicR) return 3;
				if (c.turned && c.attackingPlayer) return 2;
				if (c.explosive && !c.essentialOnly && c.attackingPlayer && p.protectBomberR > 0 && dP <= p.protectBomberR) return 1;
				return 0;
			};
			int bestR = 0;
			for (int i = 0; i < n; ++i) {
				if (!ok(i)) continue;
				const int r = rank(i);
				if (r <= 0 || r < bestR) continue;
				const bool isHeld = i == held;
				if (r > bestR || (isHeld && urgent != held) || (urgent != held && _info[i].dPlayer < _info[urgent].dPlayer)) {
					bestR = r;
					urgent = i;
				}
			}
			if (urgent >= 0) {
				sel = urgent;
				++_st.noIdleUrgent;
			}
		}
		// rotation: the held fallback target got no hit for noIdleHold s -> the others get a turn first (it rests about one
		// hold per other eligible foe, so 2 foes alternate and 3 go round)
		if (urgent < 0 && sel == held && _heldT >= p.noIdleHold - kTiny) {
			int    other = -1, nOk = 0;
			double oS = -1e300;
			for (int i = 0; i < n; ++i) {
				if (!ok(i)) continue;
				++nOk;
				if (i == held || _info[i].m->fbSkipUntil > now) continue;
				const double s = score(i);
				if (s > oS) {
					oS = s;
					other = i;
				}
			}
			if (other >= 0) {
				_info[held].m->fbSkipUntil = now + 0.9 * p.noIdleHold * std::max(1, nOk - 1);
				sel = other;
			} else {
				_heldT = 0;  // the only one: keep at it
			}
		}
		Info& in = _info[sel];
		if (in.tier == Tier::kNone) {
			const bool local = in.dCentre <= ring || cx.farHard;
			in.tier = local ? Tier::kAttacker : Tier::kFar1;
			in.reach = local ? ring : cx.maxR;
		}
		in.pickable = true;
		in.excluded = false;
		in.m->why = "noIdle";
		_fbId = cs[sel].id;
		++_st.noIdlePicks;
		// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR): a pick only the rule let through the recall window (frames)
		if (p.recallKeepPlAtkR > 0 && now - _recallAt < p.recallIgnore && now - std::max({ cs[sel].lastHitGroupAt, cs[sel].lastHitPlayerAt, cs[sel].lastHitHimAt }) > p.attackerValid)
			++_st.recallPlAtkNoIdle;
		if (!cs[sel].attackingPlayer) ++_st.d2NoIdleFights;
		return sel;
	}

	// ------------------------------------------------------------------------------------------- pick
	// DPREP-T (G2): TABS ranks from torso + velocity x 0.3 s (TD/Landfall.TABS/Unit.cs:174); there the velocity is the dash
	// AT the current target. In FO4 it is often a nav / push / door leg at 2400-3000 u/s, which put the point 720-900 u off
	// along the path, toward the other foe (live 21:29:58-30:03: 000DB75B at 1323 u beat 000DB75D at 809 u).
	// predClosing 1: only the part of his velocity toward this candidate; predHiddenZero 1: none for a hidden candidate
	// (or one never traced) and none while he flies a leg (nav / push / sky / return / AI chase / reach chain).
	Vec3 Brain::PredAt(const Facts& f, const Params& p, const Cand& c, const Mem& m) const
	{
		if (p.predHiddenZero >= 0.5 && (m.vis != 1 || _onLeg)) return f.meChest;
		if (p.predClosing >= 0.5) {
			const Vec3   to = c.chest - f.meChest;
			const double l = Length(to);
			if (l < 1e-3 || !sp::math::IsFinite(f.meVel)) return f.meChest;
			const Vec3 dir = to / l;
			return f.meChest + dir * (p.predT * std::max(0.0, Dot(f.meVel, dir)));
		}
		return f.meChest + f.meVel * p.predT;
	}

	int Brain::Pick(const Facts& f, const Params& p, const Ctx& cx, int a_cur) const
	{
		const auto& cs = f.cands;
		const int   n = static_cast<int>(cs.size());
		// K11 recall: finish the far kill only when right there, seen, with a swing due, and the player is not in
		// trouble (HUNT S6: below lowHealth he comes straight back)
		if (a_cur >= 0) {
			const Info& ci = _info[a_cur];
			if (IsFar(ci.tier) && ci.pickable && !ci.excluded && ci.m->vis == 1 && Length(cs[a_cur].chest - f.meChest) <= p.recallFinishDist &&
				f.swingDueIn <= p.recallFinishDue && cx.health >= p.lowHealth)
				return a_cur;
		}
		// HOTFIX round5C (03:39: 7 foes in combat, the player dying, he worked on one he could not hit): foes ATTACKING THE
		// PLAYER come first. In the local ring only they compete when there is one; with none local, a far one that hit the
		// player (F1) or aims at the player with a weapon drawn (F2) goes before the local foes that do not attack the player.
		// brain.defendFirst 0: round 5 C (rule zero, L4D2 score).
		// s12 fix r3 lane 3 (brain.atkFlipKeep / atkFlipNearR; s12r2 gate GH hf 130445 CZ.a 13:08:16.58-21.05, 9 switches in 7 s,
		// player 1.00 -> 0.30): the raiders the Runner turned on the player switch their combat target to HIM when he comes for them
		// ('PROBE|EXPL FF00111D ... combat=2'), so they stop 'attacking the player' and defend first drops the current one at once
		// for any other attacker of the player, seen or not ('WARDEN|pick ... FF00111D -> FF001125 d=676 vis=0 prevWhy=eligible'
		// 0.46 s after he took FF00111D at 217 u); the one he left turns back on the player (FF00111D's molotov 21.05: 0.86 -> 0.30).
		// The current target that attacked the player within atkFlipKeep s (or stands within atkFlipNearR of the player), still
		// fights the group and is seen within attackHoldR of him keeps its "attacking the player" place in the rules below: defend
		// first, attackHold and atkKeepNear (so only an urgent foe - a band-D hitter, a big monster aiming at the player, a pinner,
		// one within near4 / threat of the player - or the protect guard's hurter rules take him off it; hidden / caged ones are the
		// cage guard's). 0 / 0 = the s12r2 brain.
		bool flipKeep = false;
		if (a_cur >= 0 && (p.atkFlipKeep > 0 || p.atkFlipNearR > 0)) {
			const Info& ci = _info[a_cur];
			const Cand& cc = cs[a_cur];
			if (!cc.attackingPlayer && cc.combatWithGroup && cc.hostile && !cc.essentialOnly && !cc.killPending && ci.pickable && !ci.excluded && IsLocal(ci.tier) &&
				ci.m && ci.m->vis == 1 && Length(cc.chest - f.meChest) <= p.attackHoldR + kTiny)
				flipKeep = (p.atkFlipKeep > 0 && cx.now - ci.m->atkPlayerAt <= p.atkFlipKeep + kTiny) || (p.atkFlipNearR > 0 && ci.dPlayer <= p.atkFlipNearR + kTiny);
		}
		auto atkP = [&](int i) { return cs[i].attackingPlayer || (flipKeep && i == a_cur); };
		bool defLocal = false, defFar = false;
		if (p.defendFirst >= 0.5) {
			for (int i = 0; i < n; ++i) {
				const Info& in = _info[i];
				if (!in.pickable || in.excluded || !atkP(i)) continue;
				if (IsLocal(in.tier)) defLocal = true;
				else if (in.tier == Tier::kFar1 || in.tier == Tier::kFar2) defFar = true;
			}
		}
		// step-2 fixer (s2f; steps\s2\GATE.md, Warden band D ahead of steps 3-4):
		//   s2fBandD: while a local foe hit the player within s2fHitWin s, a current target that is hidden or does not aim at
		//     the player is left at once for the best such foe (death room r6x-120-1: the hidden shooter FF0010DE hit the
		//     player 8x in 0.6 s while he chased hidden foes that were not hitting; player to 3 %), and in the pick a foe hitting
		//     the player is never scored as behind cover and counts s2fHitBoost x nearer (a seen near target is finished first);
		//   s2fBigFirst: a seen big monster within d2DbBigR of the player competes with them (Concord r3m-120-1: the quest
		//     deathclaw was left 9.2 s behind a hidden raider he could not reach; hf2 0.03 s).
		// A current target outside that set is left at once for the best member (no hold); the new one is kept
		// s2fBandDLock s. Every hard veto, ignore and spent state still applies (pickable / excluded).
		const double s2fNow = cx.now;
		// s12 fix lane 2 (brain.bigAimR; the s12 gate's soak death 194831 and drop 220819): a big monster (not essential-only) whose
		// combat target is the player (or that hit it, Cand::attackingPlayer) within bigAimR of the player is band D from its first
		// frame, seen or not (a behemoth throws from 1500-2500 u; Swan stood 1550 / 1598 u out, outside d2DbBigR 1500). The current
		// target keeps it while it fights the group within 1.25 x bigAimR (its combat target turns to HIM once he punches it) - only
		// one that aimed at the player (_bigAimHoldId): a behemoth that only ever fought HIM is no reason to stay (the s11 gate's soak
		// death 1, 08:06:28-35: such a Swan held him 5 s while a raider's molotov killed the player)
		if (p.bigAimR > 0 && a_cur >= 0 && cs[a_cur].cls == Cls::kBig && cs[a_cur].attackingPlayer) _bigAimHoldId = cs[a_cur].id;
		// (brain.bigAimMeR: only one within that of HIM, the current target 1.25 x - s10 NK1 22:16:33: the s10 R5 big rule, shipped
		// off since, held him on Swan 3215-4066 u away, which still threw, while the raider he left killed the player)
		auto bigAim = [&](int i) {
			if (!(p.bigAimR > 0)) return false;
			const Info& in = _info[i];
			const Cand& c = cs[i];
			if (!in.pickable || in.excluded || in.tier == Tier::kNone || !in.m || c.cls != Cls::kBig || c.essentialOnly || c.killPending || !c.hostile) return false;
			// s12 r2 fix lane 1 (REVIEW_r1 2): never one in water - he cannot punch it there (as the guard's BigFinish), and a raider
			// hurting the player must not wait on it (rank 2 in the guard: protect.h BigAim refuses it the same way)
			if (c.underwater) return false;
			const bool   cur = i == a_cur && (c.attackingPlayer || (c.combatWithGroup && c.id == _bigAimHoldId));
			const double k = cur ? 1.25 : 1.0;
			// FINISH lane 6 A_big_burst (brain.bigTurnGroup; the x1 soaks 10-25 13:32 / 13:52 / 14:23 / 16:29 and 20261025-025802 03:00:09-12,
			// 070900 07:11:08-12.7: Swan 00056B7B turned ('SCAN|watch ... turned=true' 1640-1700 u from the player) with its combat target HIM
			// or a teammate ('PROBE|EXPL ... combat=2 / 1'), so neither this rule nor the guard's big rules fired and the plain score took it
			// 2.6-5.9 s later, 2500-2570 u from him - its rock went to the PLAYER 2-3 s after the turn): a big monster in combat with the
			// GROUP that has just come (a candidate for at most protectBigTurnT s, not one known before) within bigAimR of the player and
			// protectBigTurnMeR of him counts like one aiming at the player - he is on it before its first throw. The thrower-first guard
			// still beats it (core/protect.h BigFor leaves a monster busy with him to the thrower: the s11 soak death 1). 0 = off (bit exact)
			if (!cur && TurnGroupBig(p, c, in)) {
				++_bigTurnGroupN;
				return true;
			}
			if (p.bigAimMeR > 0 && Length(c.chest - f.meChest) > k * p.bigAimMeR + kTiny) return false;
			if (cur) return in.dPlayer <= k * p.bigAimR + kTiny;
			return c.attackingPlayer && in.dPlayer <= p.bigAimR + kTiny;
		};
		// (s12 fix lane 2: a local foe that hit the player within s2fHitWin - the band-D hitter whatever brain.s2fBandD says: the big
		// rule never takes him off one, and it competes with the big monster on the score)
		auto hitNowAny = [&](int i) {
			const Info& in = _info[i];
			const Cand& c = cs[i];
			return in.pickable && !in.excluded && IsLocal(in.tier) && in.m && !c.essentialOnly && !c.killPending && s2fNow - c.lastHitPlayerAt <= p.s2fHitWin + kTiny;
		};
		// s12 r3 FIX INTEGRATOR (brain.burstBigPassive; the integrator's soak 164126 dt2 16:48:12.146: 'pick 00056B7B tier=P cls=B d=2269'
		// with dp 6-8 - the passive Swan won rule zero for 5.1 s: a big monster passes the defend-first filter as an s2f big threat and
		// keeps the old score -d/prio, while every attacker of the player has the burst cost 2 x his distance + its distance to the
		// player; two raider hits then took the player 1.00 -> 0.43 -> dead): in a burst a big monster that does not attack the player,
		// does not fight the group, does not run at the player and is no newcomer is no first-strike candidate (tier P / far tiers). It waits until it
		// turns on the player (then bigAim / the burst cost rank it) or the burst is over: it is no s2f big threat then (the band-D
		// pick and the defend-first filter), and rule zero / the far tiers skip it (counted per frame x foe, _burstBigN).
		const auto burstBigPass = [&](int i) {
			if (!(_burst && p.burstBigPassive >= 0.5)) return false;
			const Cand& c = cs[i];
			const Tier  t = _info[i].tier;
			if (c.cls != Cls::kBig || c.attackingPlayer || c.combatWithGroup || !(t == Tier::kPreempt || IsFar(t))) return false;
			const double run = Dot(Flat(c.vel), FlatDir(f.playerFeet - c.feet));
			if (run >= sp::protect::kRushMinClose && run <= sp::protect::kRushMaxClose) return false;  // (it comes for the player: the Concord deathclaw)
			// (a QUIET newcomer - a candidate for at most kBigFirstHold s, not in combat - is not left out: the Concord deathclaw is quiet
			// 0.25-0.45 s out of its manhole before it runs, lane 2's R7 newcomer rule; a passive one is left out from then on. A newcomer
			// already in combat with someone else is not coming for the player: package 2's soak 173940 dt3 17:50:33.648 took Swan
			// ('P7 00056B7B inCombat=true combat=4', a candidate 0.45 s) in a 7-raider burst, the guard kept it 2.6 s, a raider hit 1.00 -> 0.40)
			const Mem* m = _info[i].m;
			// (completion side 3 s13r3, brain.newcomerKnown: one known before / there at a Reset is no newcomer here - this is a burst)
			if (!c.inCombat && m && m->firstAt > -1e8 && !m->knownOld && cx.now - m->firstAt <= sp::protect::kBigFirstHold + kTiny) return false;
			return true;
		};
		auto s2fBig = [&](int i) {
			if (bigAim(i)) return true;  // s12 fix lane 2
			if (p.s2fBigFirst < 0.5) return false;
			if (burstBigPass(i)) return false;  // s12 r3 integrator (brain.burstBigPassive)
			const Info& in = _info[i];
			const Cand& c = cs[i];
			// (seen, or hidden too: the Classify rule bigNearPlayer keeps a hidden one near the player a local candidate)
			return in.pickable && !in.excluded && IsLocal(in.tier) && in.m && c.cls == Cls::kBig && !c.essentialOnly && !c.killPending &&
			       in.dPlayer <= p.d2DbBigR;
		};
		auto s2fHit = [&](int i) {
			if (p.s2fBandD < 0.5) return false;
			const Info& in = _info[i];
			const Cand& c = cs[i];
			return in.pickable && !in.excluded && IsLocal(in.tier) && in.m && !c.essentialOnly && !c.killPending && s2fNow - c.lastHitPlayerAt <= p.s2fHitWin + kTiny;
		};
		auto s2fIn = [&](int i) { return s2fHit(i) || s2fBig(i); };
		// the current target kept inside band D: seen, aiming at the player, within attackHoldR of him
		auto s2fKeepCur = [&](int i) {
			if (i < 0 || i != a_cur) return false;
			const Info& in = _info[i];
			return in.pickable && !in.excluded && IsLocal(in.tier) && in.m && in.m->vis == 1 && cs[i].attackingPlayer && !cs[i].killPending &&
			       Length(cs[i].chest - f.meChest) <= p.attackHoldR;
		};
		bool s2fAny = false;
		for (int i = 0; i < n && !s2fAny; ++i) s2fAny = s2fIn(i);
		// the band-D scoring below (no cover, s2fHitBoost) applies only while he has no target or a weak one (hidden, or not
		// aiming at the player): on a seen attacker of the player the normal pick and its holds rule (dt 1080u 23:01: with it
		// always on, 4 shooters at ~900 u took turns as "the hitter" and he switched 22 times for 1 kill)
		const bool s2fCurWeak = a_cur < 0 || !_info[a_cur].pickable || _info[a_cur].excluded ||
		                        (!s2fIn(a_cur) && ((_info[a_cur].m && _info[a_cur].m->vis == 0) || !cs[a_cur].attackingPlayer));
		// (... or while the current one is itself a hitter, so a seen non-hitter never displaces it: the death room)
		const bool s2fScore = s2fAny && (s2fCurWeak || (a_cur >= 0 && s2fIn(a_cur)));
		if (s2fAny && p.defendFirst >= 0.5) defLocal = true;
		if (s2fAny) {
			const bool curIn = a_cur >= 0 && s2fIn(a_cur);
			const bool locked = a_cur >= 0 && cs[a_cur].id == _s2fLockId && s2fNow < _s2fLockUntil && _info[a_cur].pickable && !_info[a_cur].excluded;
			// a seen big monster also takes over from a current target that is hitting the player but HIDDEN (Concord: the hidden
			// raider 0004B2BA shot the player now and then from a spot he could not reach while the deathclaw closed in)
			bool bigOther = false;
			for (int i = 0; i < n && !bigOther; ++i) bigOther = i != a_cur && s2fBig(i);
			const bool curHidden = a_cur >= 0 && _info[a_cur].m && _info[a_cur].m->vis == 0;
			const bool bigOverHidden = bigOther && curIn && curHidden && !s2fBig(a_cur);
			// (S2F-6, a wave where everyone shoots: a SEEN current target that aims at the player and is within attackHoldR is
			// finished first - he hits it within a dash - instead of a switch each time the hitting set changes)
			// a current target outside the set is left at once only when it is hidden or does not aim at the player (death room:
			// he chased hidden non-hitters); a seen one aiming at the player is finished first (S2F-6: in a wave where everyone
			// shoots, a switch each time the hitting set changes cost 20 %); the pick below still prefers the hitters
			const bool curWeakD = a_cur >= 0 && !curIn && (curHidden || !cs[a_cur].attackingPlayer);
			// s12 fix lane 2 (brain.bigAimR): a big monster aiming at the player takes over at once from any current target that is
			// not hitting the player (nor itself such a monster) - also from a SEEN raider that only aims at the player (220819
			// 22:15:00-04.8: 4.4 s on FF00171F, no hit on the player, while Swan readied its throw), and on the first pick. A current
			// hitter is not left this way (the hurter rules and the score decide between them)
			// (never back to one the cage guard left for its slow reach: hidden, not hitting, chain memory at stage >= 2 within
			// huntIgnore - the cage guard below would take him off it again the next frame)
			bool bigAimOver = false;
			if (p.bigAimR > 0 && !curIn && !(a_cur >= 0 && hitNowAny(a_cur)))
				for (int i = 0; i < n && !bigAimOver; ++i) {
					if (i == a_cur || !bigAim(i)) continue;
					const Mem& m = *_info[i].m;
					const bool cagedMem = p.cageGuard >= 0.5 && m.vis == 0 && s2fNow - cs[i].lastHitPlayerAt > p.s2fHitWin + kTiny && m.chainStage >= 2 &&
					                      s2fNow - m.chainAt <= p.huntIgnore + kTiny;
					bigAimOver = !cagedMem;
				}
			if ((curWeakD || bigOverHidden || bigAimOver) && !locked) {
				int    bi = -1;
				double bS = -1e300;
				for (int i = 0; i < n; ++i) {
					if (!s2fIn(i) && !(bigAimOver && hitNowAny(i))) continue;  // (s12 fix lane 2: the hitters compete with the big one)
					const Mem&   m = *_info[i].m;
					const bool   occ = m.vis == 0 && (bigOverHidden || (m.occSince >= 0 && s2fNow - m.occSince >= p.occludedGrace - kTiny));
					const double prio = Priority(cs[i].cls, _info[i].pin, _info[i].dPlayer, occ, p);
					const double s = -Length(cs[i].chest - PredAt(f, p, cs[i], m)) / std::max(1e-6, prio);
					if (s > bS) {
						bS = s;
						bi = i;
					}
				}
				if (bi >= 0) {
					_s2fLockId = cs[bi].id;
					_s2fLockUntil = s2fNow + p.s2fBandDLock;
					_swChallId = 0;
					if (s2fBig(bi) && !s2fHit(bi)) ++_s2fBigN;
					else ++_s2fBandDN;
					if (bigAimOver && bigAim(bi)) {  // s12 fix lane 2
						++_bigAimN;
						_bigAimHoldId = cs[bi].id;
					}
					return bi;
				}
			}
		}
		// s4 fix lane 2 (brain.cageGuard; gate s4 item 2, the old brain in the Combat Zone: 224554 CZ.b 22:46:38-47, 224217,
		// 201650, 225526): a HIDDEN foe that is not hitting the player and needs the slow reach - the current chain at stage >= 2
		// (door AI / push / blink wait), or the chain memory on it within huntIgnore s - is "caged off": he flew round the cage
		// away from the player for 1.5-3 s while free raiders came at the player, then needed 1-2 s and a blink to come back.
		// While a FREE foe attacking the player (combat target the player, or it hit the player) is pickable, a caged current
		// target is left at once and no caged foe is picked; the free ones go hitting > seen > nearest to the player. With no
		// free attacker left the caged one is taken (the chain memory resumes at its push / blink stage). A caged foe that hits
		// the player (s2fHitWin) is not "caged": it is band D (the rules above / below).
		auto cageHit = [&](int i) { return cx.now - cs[i].lastHitPlayerAt <= p.s2fHitWin + kTiny; };
		auto caged = [&](int i) {
			const Info& in = _info[i];
			if (!in.m || in.m->vis != 0 || cageHit(i)) return false;
			if (i == a_cur && _stage >= 2) return true;
			return in.m->chainStage >= 2 && cx.now - in.m->chainAt <= p.huntIgnore + kTiny;
		};
		auto cageFree = [&](int i) {
			const Info& in = _info[i];
			const Cand& c = cs[i];
			// STEP 7.6 FIX (cageFreeSeen): a hidden foe that is not hitting the player is no 'free' attacker (it may be behind the
			// same door: picking it only starts the next slow route - the CZ.b door cascade)
			const bool freeSeen = p.cageFreeSeen < 0.5 || cageHit(i) || (in.m && in.m->vis == 1);
			return in.pickable && !in.excluded && in.m && (IsLocal(in.tier) || in.tier == Tier::kFar1 || in.tier == Tier::kFar2) && !c.essentialOnly && !c.killPending &&
			       (c.attackingPlayer || cageHit(i)) && freeSeen && !caged(i);
		};
		// s4f (fix integrator; in game hf 014237 HF3_CZ 01:46:23-27: 245 switches in 3.7 s between a caged local foe and a free far
		// attacker, 241 chain resets, 0 kills): the free attacker he was handed was not counted once it was his target, so the caged
		// one won rule zero again next frame. brain.cageGuardCur 1: the current target counts as the free attacker too (a caged foe
		// is not taken back while he is on a free one). 0 = fix lane 2 as written
		bool cageAny = false;  // a free attacker of the player is pickable (then no caged foe is picked)
		if (p.cageGuard >= 0.5) {
			for (int i = 0; i < n && !cageAny; ++i) cageAny = (i != a_cur || p.cageGuardCur >= 0.5) && cageFree(i);
			// s11 fix lane 1 (brain.cageHopSeen; sc8 ArcJet wave, 8 of 16 kills in about half the runs of every build since 10-15: the
			// hidden synths behind the side doors were handed on one by one - 11 guard switches in 17 s, each new target left the
			// moment its own route reached a door - and none was reached before they left combat): a caged target the guard HANDED
			// him (the first hop) is not left for another hidden, non-hitting foe; only a SEEN or hitting free foe takes him off
			// it. With none, cageAny is false for this frame: no switch, and rule zero does not skip the caged current one
			if (cageAny && p.cageHopSeen >= 0.5 && a_cur >= 0 && _cageHandId != 0 && cs[a_cur].id == _cageHandId && caged(a_cur)) {
				bool strict = false;
				for (int i = 0; i < n && !strict; ++i) strict = i != a_cur && cageFree(i) && (cageHit(i) || (_info[i].m && _info[i].m->vis == 1));
				if (!strict) {
					cageAny = false;
					++_cageHopKeepN;
				}
			}
			if (cageAny && a_cur >= 0 && caged(a_cur)) {
				int    bi = -1;
				double bS = 1e300;
				for (int i = 0; i < n; ++i) {
					if (i == a_cur || !cageFree(i)) continue;
					const double s = (cageHit(i) ? 0.0 : 1e6) + (_info[i].m->vis == 1 ? 0.0 : 1e5) + _info[i].dPlayer + 0.25 * Length(cs[i].chest - f.meChest);
					if (s < bS) {
						bS = s;
						bi = i;
					}
				}
				if (bi >= 0) {
					++_cageGuardN;
					_cageHandId = cs[bi].id;  // s11 fix lane 1 (brain.cageHopSeen): the hop's target
					_swChallId = 0;
					return bi;
				}
			}
		}
		const bool farFirst = !defLocal && defFar;
		// D2 tuner (GATE_D1H1 fail 1, DEFEND vs commitment: d1h-hf60 16:09:28.9-33.2 the essential FF0010DE (tier P, hidden) was
		// held until its knock-down while the provoked raider FF00110B shot the player; HF3 3/3 runs player 12.5-23 %, the
		// provoked turret never picked; hotfix 2 picks them in 0.05-0.42 s). d2DefendBreak 1: a KILLABLE foe attacking the
		// player breaks the commitment rules (attackHold / curOccFree / switchHold / occFlickerKeep) at once when the current
		// target is essential, or has had no hit for d2DbNoHit s while hidden or right at bars (d2DbCloseR). A seen big monster
		// fighting near the player (the Concord deathclaw, fail 3a) breaks a hidden current target too. No essential is picked
		// while a killable attacker of the player exists. Each break protects its new target for d2DbLock s (no ping-pong).
		bool dbEssOut = false;
		if (p.d2DefendBreak >= 0.5) {
			auto killAtk = [&](int i) {
				const Info& in = _info[i];
				const Cand& c = cs[i];
				return in.pickable && !in.excluded && in.tier != Tier::kNone && in.m && !c.essentialOnly && !c.killPending &&
				       (IsLocal(in.tier) || in.tier == Tier::kFar1 || in.tier == Tier::kFar2) && (c.attackingPlayer || cx.now - c.lastHitPlayerAt <= p.d2DbHitWin);
			};
			auto seen = [&](int i) { const Mem* m = _info[i].m; return m && m->vis == 1 && (p.occFlickerKeep <= 0 || m->occSince < 0); };
			auto bigNear = [&](int i) {
				const Info& in = _info[i];
				const Cand& c = cs[i];
				return in.pickable && !in.excluded && in.tier != Tier::kNone && in.m && !c.essentialOnly && !c.killPending && c.cls == Cls::kBig &&
				       in.dPlayer <= p.d2DbBigR && (c.inCombat || c.combatWithGroup) && seen(i);
			};
			for (int i = 0; i < n && !dbEssOut; ++i) dbEssOut = killAtk(i);
			// completion side lane 1 (brain.essHurtFirst; the user's rule Sat 02:40: the attacker closest to the player first): a
			// hurting-near essential (Update: Info::essHurt - it shot the player again since its knock-down, near it) is not left out
			// for a killable attacker of the player unless that one hit the player within d2DbHitWin too, or stands no farther from
			// the player than it + essHurtPad (a killable HITTER still comes first: the D2 case this rule was made for)
			_essFree.assign(static_cast<size_t>(n), 0);
			if (p.essHurtFirst >= 0.5) {
				for (int i = 0; i < n; ++i) {
					const Info& ei = _info[i];
					if (!ei.essHurt || !ei.pickable || ei.excluded) continue;
					bool out = false;
					// (explore m1 fix lane 3, brain.essAimFirst: an essential only that rule made 'hurting near' stays out for a killable attacker
					// that hit the player, that he SEES (the D2 order: s81 RULE-C.raider / PR-20b, the turned ex-ally and the raider that attacks
					// with it) or that stands within protectNearR of the player - never for HIDDEN ones farther out that have not hit it: the
					// raiders 1000-1500 u off behind the arena walls in HF3 06:11:51-56 kept FF0010EA out while it shot the player)
					for (int k = 0; k < n && !out; ++k)
						out = k != i && killAtk(k) &&
						      (cx.now - cs[k].lastHitPlayerAt <= p.d2DbHitWin + kTiny ||
						          (ei.essAim ? (seen(k) || _info[k].dPlayer <= p.protectNearR + kTiny) : _info[k].dPlayer <= ei.dPlayer + std::max(0.0, p.essHurtPad) + kTiny));
					_essFree[static_cast<size_t>(i)] = out ? 0 : 1;
				}
			}
			if (a_cur >= 0) {
				const Cand&  cc = cs[a_cur];
				const Info&  ci = _info[a_cur];
				const bool   curEss = cc.essentialOnly && dbEssOut && !EssFree(a_cur);  // (completion side lane 1: not a hurting-near one)
				const bool   curHid = ci.m && ci.m->vis == 0 && ci.m->occSince >= 0 && cx.now - ci.m->occSince >= p.d2DbHid - kTiny;
				const bool   noContact = cx.now - std::max(_tSince, _tHitAt) >= p.d2DbNoHit - kTiny;
				const double dCur = Length(cc.chest - f.meChest);
				// (s12 fix lane 2: never off a big monster aiming at the player for a raider - brain.bigAimR would take it back next frame)
				const bool   curWeak = curEss || (noContact && (curHid || (dCur <= p.d2DbCloseR && ci.m && ci.m->vis == 0)) && !bigAim(a_cur));
				const bool   locked = !curEss && ((cc.id == _dbLockId && cx.now < _dbLockUntil) || (cc.id == _s2fLockId && cx.now < _s2fLockUntil));  // s2f lock too
				if (curWeak && !locked) {
					int    bi = -1;
					double bS = 1e300;
					for (int i = 0; i < n; ++i) {
						if (i == a_cur) continue;
						if (s2fAny && !s2fIn(i)) continue;  // step-2 fixer: never off band D to a foe that is not hitting the player
						const bool atk = killAtk(i);
						const bool big = !atk && !cc.essentialOnly && curHid && bigNear(i);
						if (!atk && !big) continue;
						const bool sn = seen(i);
						if (!curEss && !sn) continue;  // off a hidden / bar-blocked one: only to a foe in plain sight
						const bool   recent = cx.now - cs[i].lastHitPlayerAt <= p.d2DbHitWin;
						const double s = (sn ? 0.0 : 1e6) + (recent || big ? 0.0 : 1e5) + Length(cs[i].chest - f.meChest);
						if (s < bS) {
							bS = s;
							bi = i;
						}
					}
					if (bi >= 0) {
						_dbLockId = cs[bi].id;
						_dbLockUntil = cx.now + p.d2DbLock;
						_swChallId = 0;
						++_d2DbN;
						return bi;
					}
				}
			}
			// completion side lane 1 (brain.essHurtFirst; the user's rule Sat 02:40, the attacker closest to the player first): the reverse
			// break - off a current target that does not outrank a hurting-near essential (no hit on the player within d2DbHitWin, not
			// pinning him, no big monster aiming at the player, and farther from the player than that essential + essHurtPad) onto it
			// (the one nearest the player), locked d2DbLock s like every D2 break. Without it the commitment rules kept him on a far
			// world raider he had reached while the essential shot the player. Off a HELD essential that has stopped shooting (stunned
			// by his control hit) onto another one that hit the player within d2DbHitWin (desktop HF3-HURT seed 3: 4.5 s by the quiet
			// one while the other shot the player 3 times).
			if (p.essHurtFirst >= 0.5 && a_cur >= 0) {
				const Cand& cc = cs[a_cur];
				const Info& ci = _info[a_cur];
				const bool  curHits = cx.now - cc.lastHitPlayerAt <= p.d2DbHitWin + kTiny;
				const bool  locked = (cc.id == _dbLockId && cx.now < _dbLockUntil) || (cc.id == _s2fLockId && cx.now < _s2fLockUntil);
				if (!curHits && !locked && !ci.pin && !bigAim(a_cur) && (cc.essentialOnly || !EssFree(a_cur))) {
					// (explore m1 fix lane 3, brain.essAimFirst: also off a HIDDEN killable current target that has not hit the player onto such
					// an essential he SEES - the desktop ESS-AIM scene, s81 PR-M1E: he stayed 3 s at the arena wall of a hidden raider while the
					// essential he saw shot the player 3 times; the score kept the near hidden one. An essential that only essAimFirst made
					// 'hurting near' takes part in this break only that way - never off another essential (s81 HF3-DOWNf: two never-downed
					// essentials shooting in turn flipped him 4 times))
					const bool cHid = ci.m && ci.m->vis == 0 && ci.m->occSince >= 0 && cx.now - ci.m->occSince >= p.d2DbHid - kTiny;
					int        bi = -1;
					double     bD = 1e300;
					for (int i = 0; i < n; ++i)
						if (i != a_cur && EssFree(i) && (!_info[i].essAim || (!cc.essentialOnly && cHid)) && _info[i].dPlayer < bD &&
						    (!cc.essentialOnly || cx.now - cs[i].lastHitPlayerAt <= p.d2DbHitWin + kTiny)) {
							bD = _info[i].dPlayer;
							bi = i;
						}
					const bool aimHid = p.essAimFirst >= 0.5 && bi >= 0 && !cc.essentialOnly && cHid && seen(bi);
					if (bi >= 0 && (cc.essentialOnly || ci.dPlayer > bD + std::max(0.0, p.essHurtPad) + kTiny || aimHid)) {
						_dbLockId = cs[bi].id;
						_dbLockUntil = cx.now + p.d2DbLock;
						_swChallId = 0;
						++_d2DbN;
						++_essHurtPickN;
						return bi;
					}
				}
			}
		}
		// D2 tuner (gate D1H hf120-b HF3 16:57:50-53: he worked the door / blink chain on a HIDDEN foe behind the Combat Zone
		// door for 3 s while a provoked raider and a turret in sight shot the player from 100 % to 12 %; D1H cz120 CZ.b the
		// same, 7 s on one raider): while the reach chain on the hidden current target is past its fast stages (stage >= 2:
		// door AI / blink wait / blink cap / push), a SEEN local foe attacking the player is not held off by the commitment
		// rules: the current target loses its occlusion pass (curOccFree) and the seen attacker switches at once (no
		// switchHold). Its chain is remembered (chainMemory): re-picked later it resumes where it was. 0 = D1H.
		bool slowCur = false;
		if (p.d2SlowSwitch >= 0.5 && a_cur >= 0 && _stage >= 2 && _info[a_cur].m && _info[a_cur].m->vis == 0) {
			const Mem& cm = *_info[a_cur].m;
			const bool hidLong = cm.occSince >= 0 && cx.now - cm.occSince >= p.occludedGrace - kTiny;
			if (hidLong) {
				for (int i = 0; i < n && !slowCur; ++i) {
					const Info& in = _info[i];
					if (i == a_cur || !in.pickable || in.excluded || !IsLocal(in.tier) || !in.m || !cs[i].attackingPlayer) continue;
					if (in.m->vis == 1 && (p.occFlickerKeep <= 0 || in.m->occSince < 0)) slowCur = true;  // seen (no hidden clock left)
				}
			}
		}
		// DPREP-T (G3; live 21:38:34.4: 'HUNT|attack 0011B951 d=698' on the mutant hound, then 'end why=threat' 0.25 s later,
		// no hit; 21:38:32.7 the same on 0011B952 at d=684): the current target, seen within attackHoldR, is kept until it is
		// hit or for attackHoldFor s (one TABS swing carries ~550 u; TABS first hit at 10 m is 0.28 s), before rule zero.
		// It yields to a local foe nearer to his predicted point, a pinner, or a foe hitting the player within threat.
		if (p.attackHoldR > 0 && a_cur >= 0 && !((defLocal || farFirst) && !atkP(a_cur))) {  // ROUND5 D1 merge: HF defend first wins (s12 fix r3 lane 3: atkP = + the flip keep)
			const Info& ci = _info[a_cur];
			const Cand& cc = cs[a_cur];
			if (ci.pickable && !ci.excluded && ci.tier != Tier::kNone && ci.m && ci.m->vis == 1 && Length(cc.chest - f.meChest) <= p.attackHoldR) {
				if (_holdId != cc.id) {
					_holdId = cc.id;
					_holdSince = cx.now;
				}
				if (cx.now - _holdSince < p.attackHoldFor - kTiny && _tHitAt < _holdSince - kTiny) {
					const double curPD = Length(cc.chest - PredAt(f, p, cc, *ci.m));
					bool         yield = false;
					for (int i = 0; i < n && !yield; ++i) {
						const Info& in = _info[i];
						if (i == a_cur || !in.pickable || in.excluded || in.tier == Tier::kNone || !in.m) continue;
						const Cand& c = cs[i];
						if (in.pin || (cx.now - c.lastHitPlayerAt <= p.attackerValid && in.dPlayer < p.threat)) yield = true;
						else if (IsLocal(in.tier) && Length(c.chest - PredAt(f, p, c, *in.m)) < curPD) yield = true;
					}
					if (!yield) {
						++_attackHoldN;
						return a_cur;
					}
				}
			}
		}
		// rule zero: the local ring first, L4D2 score every frame, no hysteresis (switchMargin 0)
		int    best = -1;
		double bestS = -1e300, curS = -1e300;
		bool   localBlocks = false;  // rule zero: a leashed local foe still keeps him from flying off (K11 recall)
		const bool flickKeep = p.occFlickerKeep > 0;
		// s12 fix lane 2 (brain.bigAimR): on a big monster aiming at the player only band D competes (a foe hitting the player, another
		// such monster) - a raider that only aims would win the L4D2 score after switchHold and the big rule take him back (ping-pong)
		const bool curBigAim = a_cur >= 0 && bigAim(a_cur);
		for (int i = 0; i < n; ++i) {
			const Info& in = _info[i];
			if (in.pickable && IsLocal(in.tier) && in.leashed && (!farFirst || cs[i].attackingPlayer)) localBlocks = true;
			if (!in.pickable || in.excluded || !IsLocal(in.tier)) continue;
			if (burstBigPass(i)) {  // s12 r3 integrator (brain.burstBigPassive)
				++_burstBigN;
				continue;
			}
			if (farFirst || (defLocal && !atkP(i) && !s2fIn(i))) continue;  // HOTFIX round5C: defend first (s2f: + big threats; s12 fix r3 lane 3: + the flip keep)
			if (flipKeep && i == a_cur && defLocal && !s2fIn(i)) ++_flipKeepN;              // s12 fix r3 lane 3 (frames it kept its place)
			if (curBigAim && !s2fIn(i) && !hitNowAny(i)) continue;                         // s12 fix lane 2
			if (cageAny && caged(i)) {  // s4 fix lane 2 (brain.cageGuard): free attackers of the player first
				++_cageSkipN;
				continue;
			}

			if (dbEssOut && cs[i].essentialOnly && EssFree(i)) ++_essHurtPickN;  // completion side lane 1 (brain.essHurtFirst): let through
			if (dbEssOut && cs[i].essentialOnly && !EssFree(i)) {  // D2 tuner: never an essential while a killable attacker of the player exists
				++_d2EssSkipN;
				continue;
			}
			const Mem& m = *in.m;
			// occluded: hidden for occludedGrace s. DPREP-T (G1, occFlickerKeep > 0): the hidden clock survives short visible
			// runs (SetVis), so an alternative counts as seen only after occFlickerKeep s of continuous sight; the current
			// target is judged on this frame's sight (as round 5 B) but a flicker no longer gives it a fresh grace either.
			const bool hidLong = m.occSince >= 0 && cx.now - m.occSince >= p.occludedGrace - kTiny;
			bool       occ = (!flickKeep || i == a_cur) ? (m.vis == 0 && hidLong) : hidLong;
			// REVIEW-B / DPREP-T (G1): the committed current target (or the one the reach chain works on, stage 0-3) keeps its
			// full priority while hidden; before, after occludedGrace it counted 6.7x farther and lost to any foe that
			// flickered into sight (CZ cage bars; live 21:29:55 two hidden synths behind a door)
			if (p.curOccFree >= 0.5 && i == a_cur && (in.committed || (_stage >= 0 && _stage <= 3)) && !slowCur) occ = false;  // D2 tuner: not while slowCur
			// step-2 fixer (band D): a foe hitting the player now is never scored as "behind cover", and counts s2fHitBoost x nearer
			const bool hitNowI = s2fScore && s2fHit(i);
			if (hitNowI) occ = false;
			const double prio = Priority(cs[i].cls, in.pin, in.dPlayer, occ, p) * (hitNowI ? std::max(1.0, p.s2fHitBoost) : 1.0);
			double       s = -Length(cs[i].chest - PredAt(f, p, cs[i], m)) / std::max(1e-6, prio);
			// s12 r3 fix lane 1 (brain.burstDp; the s12r2 gate soaks GK1 dt3 20261023-122205 12:32:50.66 and GK2 dt2 -124815 12:54:58.82,
			// the downtown 10-raider bursts, dp 5-8: right after a kill the score took FF001836 2067 u from him and FF0015D2 1954 u from
			// him / 1461 u from the player - the L4D2 score ranks by HIS distance, the player's only as a 1-1.25x factor beyond 513 u -
			// while the raiders that then hurt the player stood 700-1300 u from it): in a burst a foe attacking the player is ranked by
			// its distance to the PLAYER - where it will be in burstLead s at its own run speed toward the player (the melee rusher),
			// + burstLosPen without a clear line to the player (Update's ray cache) - + burstMeW x his own distance (x 1 / occludedPrio
			// while hidden from him: the slow reach). A hidden tier-E foe (no hit on the group within attackerValid) beyond burstHidR of
			// the player is set last (GK2 54:58.82 FF0015D2 hidden 1954 u). Pin / band-D boosts and the big-monster class stay.
			// FINISH lane 6 A_big_burst (brain.burstChainPl; K07_ANALYSIS row 2: the tier-E kill chain went outward through the burst by HIS
			// distance - soak 20261022-003129 dt3 00:42:44.0-46.2 FF001A16 839 -> FF001A12 1212 -> FF001A10 317 -> FF0019DE 1649 u from him):
			// in a burst (burstDp attackers of the player, or the burst leash's burstLeashN hostiles near the player) a local candidate that
			// does NOT attack the player (tier E / P: it fights HIM or nobody yet) is ranked by the same burst cost as an attacker - its
			// distance to the PLAYER + burstMeW x his own - so after every kill the next one is the foe nearest the player he can get to,
			// not the nearest to him. (Attackers of the player still come first: the defend-first filter above lets non-attackers in only
			// when no attacker is pickable.) 0 = off (bit exact)
			const bool chainPl = p.burstChainPl >= 0.5 && !cs[i].attackingPlayer &&
			                     (_burst || (p.burstLeashR > 0 && _burstNear >= static_cast<int>(p.burstLeashN + 0.5)));
			if (chainPl) ++_burstChainN;
			if ((_burst && cs[i].attackingPlayer) || chainPl) {
				const Cand&  c = cs[i];
				const double dMe = Length(c.chest - PredAt(f, p, c, m));
				const double run = Dot(Flat(c.vel), FlatDir(f.playerFeet - c.feet));
				const double lead = (run >= sp::protect::kRushMinClose && run <= sp::protect::kRushMaxClose) ? run * std::max(0.0, p.burstLead) : 0.0;
				double       dPl = std::max(0.0, in.dPlayer - lead);
				if (p.burstLosPen > 0 && !hitNowI && m.plLosAt > -1e8 && cx.now >= m.plLosAt && cx.now - m.plLosAt <= 1.0 + kTiny && !m.plLos) dPl += p.burstLosPen;
				double cost = std::max(0.0, p.burstMeW) * dMe * (occ ? 1.0 / std::max(1e-6, p.occludedPrio) : 1.0) + dPl;
				if (p.burstHidR > 0 && in.tier == Tier::kEngaged && m.vis == 0 && in.dPlayer > p.burstHidR + kTiny && cx.now - c.lastHitPlayerAt > p.hitMemory + kTiny) {
					cost += 1e7;
					++_burstHidN;
				}
				const double base = (c.cls == Cls::kBig ? std::max(1.0, p.prioBig / std::max(1e-6, p.prioRanged)) : 1.0) * (in.pin ? p.pinPrio : 1.0) *
				                    (hitNowI ? std::max(1.0, p.s2fHitBoost) : 1.0);
				s = -cost / std::max(1e-6, base);
			}
			if (i == a_cur) curS = s;
			if (s > bestS) {
				bestS = s;
				best = i;
			}
		}
		if (best >= 0) {
			// s12 r2 fix lane 1 (brain.protNearR; s12r1 soak death K4 cz1 05:11:34.26-38.5: the protected Combat Zone gunner FF001251,
			// fighting HIM 400-500 u away and hidden from him, lost the score to the hidden raider FF001312 987 u away; it turned on
			// the player and shot it 1.00 -> 0.81 -> 0.52 -> dead in 1.5 s while he finished two raiders that fought only him): a
			// protected / essential current target near the player with a clear line to it (Update: _protNearCur) is a threat before
			// its first shot - the score never takes him off it for a foe that neither attacks the player, nor hit it, nor pins him
			// (those still switch, and every rule above - band D, the defend break, the cage guard - and the guard's hurter rules too)
			if (_protNearCur && a_cur >= 0 && a_cur != best && curS > -1e299 && !cs[best].attackingPlayer && !_info[best].pin &&
				!(cx.now - cs[best].lastHitPlayerAt <= p.hitMemory + kTiny)) {
				++_protNearN;
				_swChallId = 0;
				return a_cur;
			}
			// s12 r3 fix lane 1 (brain.burstKeepA; the s12r2 gate soak GK1 dt3 12:32:54.23-54.78: the raider FF00182A hit the player and
			// was dropped 4 times in 0.55 s for nearer raiders that had not): in a burst a current target that hit the player within
			// burstKeepA s (not essential-only; it passed every filter of the loop: curS) is not left on the score for a foe that did
			// not - unless that one pins him, stands within threat of the player or is a big monster coming for the player. Death /
			// kill-pending / a give-up end it as before (not pickable), and the guard's hurter rules still act.
			if (_burst && p.burstKeepA > 0 && a_cur >= 0 && a_cur != best && curS > -1e299) {
				const Cand& cc = cs[a_cur];
				const Info& bi = _info[best];
				const bool  curA = !cc.essentialOnly && !cc.killPending && cx.now >= cc.lastHitPlayerAt - kTiny && cx.now - cc.lastHitPlayerAt <= p.burstKeepA + kTiny;
				const bool  bestA = cx.now >= cs[best].lastHitPlayerAt - kTiny && cx.now - cs[best].lastHitPlayerAt <= p.burstKeepA + kTiny;
				// (a SEEN foe running at the player that reaches its melee reach within burstLead s - the blow he can still stop - is
				// not held off either: desktop BURST, the rushers of DT3 at 3 x damage)
				const double runB = Dot(Flat(cs[best].vel), FlatDir(f.playerFeet - cs[best].feet));
				const bool   rushB = bi.m && bi.m->vis == 1 && cs[best].attackingPlayer && runB >= sp::protect::kRushMinClose && runB <= sp::protect::kRushMaxClose &&
				                   (bi.dPlayer - std::clamp(p.protectRushReach, 0.0, 1000.0)) / runB <= p.burstLead + kTiny;
				if (curA && !bestA && !bi.pin && !(bi.dPlayer < p.threat) && !s2fBig(best) && !rushB) {
					++_burstKeepN;
					_swChallId = 0;
					return a_cur;
				}
			}
			// s12 r3 fix lane 1 (brain.blinkHoldScore; K01: 9 of the s12r2 candidate's 71 soak reach blinks were left for another foe
			// within 2 s without a kill - 'eligible' score switches 0.01-1.75 s after the landing, e.g. 104556 10:53:47.65 / 49.73, 110623
			// 11:10:40.79 - so the blink was wasted and the next chain blinked again): for blinkKeepFor s after his own blink to the
			// current target the score does not take him off it for a foe that is not urgent (a pinner, within threat of the player, a
			// band-D hitter, a big monster coming for the player)
			if (p.blinkHoldScore >= 0.5 && a_cur >= 0 && a_cur != best && curS > -1e299 && cs[a_cur].id == _blinkKeepId && cx.now < _blinkKeepUntil) {
				const Info& bi = _info[best];
				if (!bi.pin && !(bi.dPlayer < p.threat) && !hitNowAny(best) && !s2fBig(best)) {
					++_blinkHoldN;
					_swChallId = 0;
					return a_cur;
				}
			}
			if (p.switchMargin > 0 && a_cur >= 0 && a_cur != best && curS > -1e299 && bestS < curS + std::abs(curS) * p.switchMargin) {
				_swChallId = 0;  // REVIEW-B: the current one won this frame: a challenger starts its hold again
				return a_cur;
			}
			// s12 fix r2 lane 2 (brain.atkKeepNear; s12r1 gate soak GK2 064252 06:49:22.01-24.73, the 10-raider burst, dp 6-7: he took
			// FF0015B5 - seen, aiming at the player - 1070 u from him, and 0.76 s later the L4D2 score (after switchHold) took him to
			// FF0015B0 1719 u away, a foe nearer to the PLAYER but not hitting it; FF0015B5 then shot the player 3x, 1.00 -> 0.50, and
			// he made 10 picks in 8 s): a SEEN local current target attacking the player is finished first - not left on the score
			// for a foe FARTHER from him. Urgent ones still switch at once: a pinner, one within near4 of the player, a foe hitting
			// the player now (band D) or a big monster aiming at it. curS > -1e299: the current one passed every filter of the loop
			// (not caged off, not an essential while killable attackers are up, defend first) - those rules still take him off it.
			if (p.atkKeepNear >= 0.5 && a_cur >= 0 && a_cur != best && curS > -1e299) {
				const Info& ci = _info[a_cur];
				const Cand& cc = cs[a_cur];
				const Info& bi = _info[best];
				const bool  curAtk = ci.m && ci.m->vis == 1 && atkP(a_cur) && !cc.essentialOnly && !cc.killPending && IsLocal(ci.tier);
				const bool  urgentB = bi.pin || bi.dPlayer < p.near4 || hitNowAny(best) || s2fBig(best) || (s2fScore && s2fIn(best) && !s2fIn(a_cur));
				if (curAtk && !urgentB && Length(cs[best].chest - f.meChest) > Length(cc.chest - f.meChest)) {
					_swChallId = 0;
					++_atkKeepN;
					return a_cur;
				}
			}
			// REVIEW-B (user 21:32: "keep the committed target unless the alternative is clearly better ... for at least
			// 0.5 s"): a challenger must win for switchHold s in a row. A pinner or a foe within threat of the player switches
			// at once. curS <= -1e299 = the current one is not pickable / excluded this frame: no hold.
			if (p.switchHold > 0 && a_cur >= 0 && a_cur != best && curS > -1e299) {
				const Info&  bi = _info[best];
				const double dB = Length(cs[best].chest - f.meChest), dC = Length(cs[a_cur].chest - f.meChest);
				// "unless a much closer attacker appears" (user 21:15): a SEEN challenger at under switchUrgentD x the current
				// distance (raw distance, no occlusion factor: a flicker never qualifies by itself; DPREP-T: with
				// occFlickerKeep, "seen" = no hidden clock left, i.e. never hidden or seen occFlickerKeep s in a row)
				const bool seenB = bi.m && bi.m->vis == 1 && (!flickKeep || bi.m->occSince < 0);
				const bool slowWin = slowCur && seenB && cs[best].attackingPlayer;  // D2 tuner: off a hidden target's slow chain
				const bool urgent = bi.pin || bi.dPlayer < p.threat || (p.switchUrgentD > 0 && seenB && dB < p.switchUrgentD * dC) || slowWin ||
				                    (s2fScore && s2fIn(best) && !s2fIn(a_cur) && !s2fKeepCur(a_cur));  // step-2 fixer: band D / big first never waits (weak current only)
				if (slowWin) ++_d2SlowSwN;
				if (!urgent) {
					if (_swChallId != cs[best].id) {
						_swChallId = cs[best].id;
						_swSince = cx.now;
					}
					if (cx.now - _swSince < p.switchHold - kTiny) {
						++_switchHoldN;
						return a_cur;
					}
				}
			}
			_swChallId = 0;
			return best;
		}
		if (localBlocks) return -1;
		// far tiers: tier order, then nearest to the player (chain bonus near him), committed to the current one
		const bool chain = cx.now - _lastFarEndAt <= p.chainWindow;
		// s13 fix round 3 lane 5 (brain.chainNearR; the s13r2 gate's ghoul swarm sc1: hit rate 0.21 / 0.25 against the installed 0.50, all
		// dead 7.7 / 6.8 s against 4.1 s; every re-pick after a kill 1250-2370 u from him - 'SPN|ARR d0=' - each 5-7 swings at 'reach' on
		// the way, while the installed build chained 342-467 u). Below, the far tiers go by rank, then by the distance to the PLAYER (the
		// chain bonus only for a foe of at least the last rank), and with defendFirst (farFirst) a far foe whose combat target is the player
		// goes before every other far foe. In a melee swarm beyond the guard ring the foes around him fight HIM (F3 / F1 'hit him'), so the
		// ones still running at the player are by definition elsewhere: after every kill he flew across the swarm ('WARDEN|pick ... d=2156'
		// with the next ghoul 16-60 u from him, 'CONTACT|col ... d=16'). Within the chain window after a far kill (and on the foe this rule
		// picked), while no far foe hurts the player (F1 for a hit on the player / the group within hitMemory, or a ranged, big or throwing
		// foe aiming at it) and the player is above lowHealth, the far foe nearest HIM within chainNearR that is not known hidden goes next (the
		// current one is kept within 1.5 x). A melee foe running at the player meanwhile is still outside the guard ring; when it gets there
		// it is a local foe and rule zero takes it. The protect guard (ProtectFilter) still has the last word. 0 = off (the old order).
		if (p.chainNearR > 0 && cx.health >= p.lowHealth && (chain || (a_cur >= 0 && _chainNearId != 0 && cs[a_cur].id == _chainNearId))) {
			bool hurt = false;
			for (int i = 0; i < n && !hurt; ++i) {
				const Info& in = _info[i];
				const Cand& c = cs[i];
				if (!in.pickable || in.excluded || !IsFar(in.tier) || c.killPending) continue;
				hurt = (in.tier == Tier::kFar1 && in.sub < 2) || (c.attackingPlayer && (c.cls != Cls::kRegular || c.explosive));
			}
			int    cn = -1;
			double cnK = 1e300;
			for (int i = 0; i < n && !hurt; ++i) {
				const Info& in = _info[i];
				const Cand& c = cs[i];
				if (!in.pickable || in.excluded || !IsFar(in.tier) || in.tier == Tier::kFarP || c.killPending || c.essentialOnly) continue;
				if ((in.m && in.m->vis == 0) || burstBigPass(i)) continue;
				const bool   cur = i == a_cur;
				const double dm = Length(c.chest - f.meChest);
				if (dm > (cur ? 1.5 : 1.0) * p.chainNearR + kTiny) continue;
				const double k = cur ? dm - 0.5 * p.chainNearR : dm;
				if (k < cnK) {
					cnK = k;
					cn = i;
				}
			}
			if (cn >= 0) {
				++_chainNearN;
				_chainNearId = cs[cn].id;
				return cn;
			}
			_chainNearId = 0;
		}
		int        fb = -1, fbRank = 0, fbSub = 9;
		double     fbD = 1e300;
		for (int i = 0; i < n; ++i) {
			const Info& in = _info[i];
			if (!in.pickable || in.excluded || !IsFar(in.tier)) continue;
			if (farFirst && !(cs[i].attackingPlayer && (in.tier == Tier::kFar1 || in.tier == Tier::kFar2))) continue;  // HOTFIX round5C
			if (burstBigPass(i)) {  // s12 r3 integrator (brain.burstBigPassive)
				++_burstBigN;
				continue;
			}
			if (dbEssOut && cs[i].essentialOnly && !EssFree(i)) continue;  // D2 tuner (completion side lane 1: + not a hurting-near one)
			if (cageAny && caged(i)) {                                                                                      // s4 fix lane 2
				++_cageSkipN;
				continue;
			}
			const int rank = FarRank(in.tier);
			double    d = in.dPlayer;
			if (chain && rank >= FarRank(_lastFarTier) && Length(cs[i].chest - f.meChest) <= p.chainR) d -= p.chainR;
			if (rank > fbRank || (rank == fbRank && (in.sub < fbSub || (in.sub == fbSub && d < fbD)))) {
				fb = i;
				fbRank = rank;
				fbSub = in.sub;
				fbD = d;
			}
		}
		if (a_cur >= 0) {
			const Info& ci = _info[a_cur];
			// K10 commitment (HOTFIX round5C: never over a far foe shooting the player while the current one does not)
			if (IsFar(ci.tier) && ci.pickable && !ci.excluded && fbRank <= FarRank(ci.tier) && (!farFirst || cs[a_cur].attackingPlayer || fb < 0)) {
				// ROUND5 A2 (gate A1, MQ102 bloatflies at 60 fps: 9 s chasing one bug hidden behind a house - sky, push, blink
				// wait - while the other bugs flew in plain sight; 13.6 s without a kill): a committed far target hidden for
				// farSwitchSeen s gives way to a SEEN far foe of at least its rank that is clearly nearer to him (0.7 x). Each
				// switch needs that much hidden time again, so no ping-pong. 0 = round 5 A (commitment always).
				if (p.farSwitchSeen > 0 && ci.m && ci.m->vis == 0 && _hiddenTotal >= p.farSwitchSeen - kTiny) {
					const double dCur = Length(cs[a_cur].chest - f.meChest);
					int          alt = -1;
					double       altD = 0.7 * dCur;
					for (int i = 0; i < n; ++i) {
						const Info& in = _info[i];
						if (i == a_cur || !in.pickable || in.excluded || !IsFar(in.tier) || FarRank(in.tier) < FarRank(ci.tier) || !in.m || in.m->vis != 1) continue;
						const double di = Length(cs[i].chest - f.meChest);
						if (di < altD) {
							altD = di;
							alt = i;
						}
					}
					if (alt >= 0) return alt;
				}
				return a_cur;
			}
		}
		return fb;
	}

	// ------------------------------------------------------------------------------------------- sight (L4D2 ClearTo)
	bool Brain::TargetSight(const Cand& c, Mem& m, const Facts& f, const Params& p, IWorld& w, int a_maxRays)
	{
		if (Length(c.chest - f.meEye) > p.sightMax) {
			SetVis(m, false);
			return false;
		}
		const Vec3 head = HeadOf(c);
		// R6 step 7 (look.sightFromFist, TABS CanSeeSystem.cs:54-70: the sight ray starts at his right fist): with a valid sim
		// fist the first ray is fist -> chest, then eye -> chest and eye -> head (the same 3-ray budget). No fist = the old three.
		// look.sightOverTop > 0: a 4th ray from that high above his eye to its head (over cars / fences / rocks), only for the
		// dash target and only when the others are blocked. The target's rays are P1 (the engine may not refuse them); a
		// refused ray (look.sightRefusedKeep) is unknown: it neither shows nor hides the foe.
		const bool fist = f.haveFist && sp::math::IsFinite(f.simFist);
		Vec3       from[4] = { fist ? f.simFist : f.meEye, f.meEye, fist ? f.meEye : f.meChest, f.meEye + Vec3{ 0, 0, f.sightOverTop } };
		Vec3       to[4] = { c.chest, fist ? c.chest : head, fist ? head : c.chest, head };
		const int  nRays = std::min(f.sightOverTop > 0 ? 4 : 3, a_maxRays + (f.sightOverTop > 0 ? 1 : 0));
		bool       traced = false, refused = false;
		w.SightPriority(true);
		for (int k = 0; k < nRays; ++k) {
			if (!CanCast(1)) break;
			if (RClearTo(w, from[k], to[k], c.id, p.sightReach)) {
				w.SightPriority(false);
				SetVis(m, true);
				return true;
			}
			if (w.LastRefused()) {
				refused = true;
				continue;
			}
			traced = true;
		}
		w.SightPriority(false);
		if (!traced) return m.vis != 0;  // no ray budget left / every ray refused: the cache (unknown counts as seen, L4D2)
		(void)refused;
		SetVis(m, false);
		return false;
	}

	void Brain::RoundRobin(const Facts& f, const Params& p, IWorld& w)
	{
		const auto& cs = f.cands;
		const int   n = static_cast<int>(cs.size());
		if (n == 0) return;
		const int want = std::min(static_cast<int>(p.visPerFrame), static_cast<int>(p.rayBudget) - _rays);
		int       done = 0;
		for (int k = 0; k < n && done < want; ++k) {
			_rr = (_rr + 1) % n;
			const Cand& c = cs[_rr];
			Info&       in = _info[_rr];
			if (c.id == _target || !in.sightful || in.m == nullptr) continue;
			Mem&         m = *in.m;
			const double d = Length(c.chest - f.meEye);
			if (_now - m.visAt < (d > p.farSightDist ? p.farVisAge : p.visAge) - kTiny) continue;
			if (d > p.sightMax) {
				SetVis(m, false);
				continue;
			}
			if (!CanCast(1)) break;
			// R6 step 7 (look.sightFromFist): from his sim right fist like TABS; a refused pick (look.sightRefusedKeep) keeps the
			// last answer and ends this frame's round (the engine's pick time is used up)
			const bool clear = RClearTo(w, f.haveFist && sp::math::IsFinite(f.simFist) ? f.simFist : f.meEye, c.chest, c.id, p.sightReach);
			++done;
			if (!clear && w.LastRefused()) break;
			SetVis(m, clear);
		}
	}

	// ------------------------------------------------------------------------------------------- blink
	const Vec3* Brain::RefusedLandingFor(const Cand& c, const Params& p) const
	{
		if (!(p.blinkUndoneT > 0 && p.blinkUndoneR > 0 && _undoneAt > -1e8 &&
			_now >= _undoneAt - kTiny && _now - _undoneAt < p.blinkUndoneT - kTiny)) return nullptr;
		return BlinkProtectsPlayer(c, p, _now) ? nullptr : &_undoneLand;
	}

	bool Brain::TryBlink(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out, int a_why)
	{
		// R6 step 10 ([R6] Tele 1, ARCH §4.11): the pin / hunt / s2f blinks of the old movement are not teleports he may make.
		// Only the Pilot's permitted defend jump at this very target (the shield case that held long enough) may go from here;
		// else refused: the caller's old refusal path (the pin keeps its target, a hunt gives up / re-reaches, and an attacker
		// of the player that stays unreachable reaches its BLINK stage, where it is shielded)
		if (TeleOn(f)) {
			if (f.tele.jumpId != 0 && c.id == f.tele.jumpId && _now - _lastBlinkAt >= p.blinkGap - kTiny && _now - _djTryAt >= p.blinkGap - kTiny) {
				++_st.teleJumpAsks;
				if (DefendJump(c, f, p, w, out)) return true;
			}
			_lastBlinkTryAt = _now;
			++_st.teleBlinkRefused;
			return false;
		}
		_lastBlinkTryAt = _now;
		// R6 step 4 (Facts::ext.on, ARCH §4.11): the old movement's pin / hunt / defend blinks are not teleports he may make;
		// the Warden's defend jump is DefendJump (why 5). tele.oldReachBlinks 1: the D2 rules, both ends out of view.
		if (!ExtBlinkOk(c, f)) {
			++_st.extBlinkRefused;
			return false;
		}
		_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
		// D2: exterior / seen take-off (pin and hunt blinks too)
		const double hp = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
		// R6 step 8.1 (brain.protectPinBlink; s8c soak cz2 11:31:33-37: a melee raider beat the player 0.72 -> 0.28 behind the
		// arena wall while he waited 2.8 s for an unseen moment; desktop PR-08 4.2 s): a PIN blink (a_why 1: the foe is at the
		// player, the pin rule) toward a foe that hit the player within d2DbHitWin s is a defend blink from the first hit, not
		// only below d2BlinkSeenDefendHp of health (seen take-off / landing allowed). The exterior ban stays.
		// fix lane 1 (REVIEW 4): a pin blink made "defend" by this rule ALONE keeps the blink budget (d2BlinkBudget per
		// d2BlinkWindow, shared with every other blink): before it was unbudgeted at any health - untested, visible teleports
		// while the player is at 85-100 %. Only the old rule (the player below d2BlinkSeenDefendHp) still skips the budget.
		const bool   pinHurt = !f.ext.on && p.protectPinBlink >= 0.5 && a_why == 1 && c.attackingPlayer && c.lastHitPlayerAt > -1e8 &&
		                     _now >= c.lastHitPlayerAt && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny;
		const bool   defendHp = !f.ext.on && p.d2BlinkSeenDefendHp > 0 && c.attackingPlayer && hp < p.d2BlinkSeenDefendHp;  // D2: survival first
		const bool   defend = defendHp || pinHurt;
		if (pinHurt && !defendHp) ++_st.protPinBlinks;  // (tries the old rule would not have made defend)
		// R6 step 9 ([R6] Route 1, ARCH 4.11): no pin / hunt blink unless it is a D2 defend blink (the permitted defend jump
		// until step 10); the Router flies him there instead
		if (f.route.on && f.route.noReachBlinks && !defend) {
			++_st.routeBlinkRefused;
			return false;
		}
		if (!D2BlinkAllowed(f, p, w, Vec3{ std::nan(""), 0, 0 }, false, defend)) return false;
		if (pinHurt && !defendHp && p.d2BlinkBudget > 0 && D2BlinksInWindow(p) >= static_cast<int>(p.d2BlinkBudget)) {  // fix lane 1: its budget
			++_st.d2BlinkBudgetNo;
			return false;
		}
		if (f.ext.on && PlayerSees(f, p, w, f.meChest)) {  // R6 step 4: never a seen take-off
			++_st.extBlinkRefused;
			return false;
		}
		Vec3 feet{};
		int  used = 0;
		bool rejectedLanding = false;
		const bool ok = BlinkSpot(c, f.meFeet, p, w, feet, used, std::min(8, _rayCap - _rays),
			RefusedLandingFor(c, p), p.blinkUndoneR, &rejectedLanding);
		_rays += used;
		if (!ok) {
			if (rejectedLanding) {
				M(c.id).undoneRefusalAt = _now;
				++_st.blinkUndoneNo;
			}
			return false;
		}
		if ((p.d2BlinkSeen < 0.5 || f.ext.on) && !defend && PlayerSees(f, p, w, feet + Vec3{ 0, 0, std::max(0.0, f.meChest.z - f.meFeet.z) })) {  // D2: a seen landing
			++_st.d2BlinkSeen;
			return false;
		}
		out.blink = true;
		out.blinkFeet = feet;
		out.blinkWhy = a_why;
		out.blinkDefend = defend;  // R6 step 9
		_lastBlinkAt = _now;
		D2NoteBlink();  // D2: the blink budget
		_pinBlockedSince = -1;
		_best = _farBest = 1e18;
		_progAt = _farProgAt = _now;
		_noPathSince = _exhaustedSince = -1;
		_stuckSince = -1;
		if (a_why == 1) ++_st.pinBlinks;
		else {
			++_st.huntBlinks;
			Mem& m = M(c.id);
			for (int k = 3; k > 0; --k) m.huntBlinkAt[k] = m.huntBlinkAt[k - 1];
			m.huntBlinkAt[0] = _now;
		}
		return true;
	}

	// ------------------------------------------------------------------------------------------- R6 step 4: the Warden picks
	// ARCHITECTURE.md S1 part 2 / §4.11 applied to the old mover (Facts::ext.on). See WardenExt in brain.h.
	bool Brain::ExtBlinkOk(const Cand& /*c*/, const Facts& f) const
	{
		// the old movement's own blinks (pin / hunt / reach stage / hidden-cap): never, unless tele.oldReachBlinks 1 (then the
		// D2 rules, both ends out of view: TryBlink / the BLINK stage check it). The Warden's defend jump is DefendJump.
		return !f.ext.on || f.ext.oldReachBlinks;
	}

	void Brain::ExtReachFail(const Cand& c, GiveUp a_why, Out& out)
	{
		// the old give-up's log / bookmark line stays (brain_glue "give up <id> <why>"), but nothing is ignored
		out.giveUp = a_why;
		out.giveUpId = c.id;
		const double gap = _curF ? std::max(0.0, _curF->ext.failGap) : 1.0;
		if (_extFailId != c.id || _now - _extFailAt >= gap - kTiny || _now < _extFailAt) {
			out.reachFail = true;
			out.reachFailId = c.id;
			out.reachFailWhy = GiveUpName(a_why);
			_extFailId = c.id;
			_extFailAt = _now;
			++_st.extFails;
		}
		// the old movement starts over on it: a fresh reach chain (navmesh first), fresh progress / stuck / blind clocks. The
		// hunt edges, the AI-chase request and the per-target blink count are kept (one HUNT|attack, one OnSPNChase per target).
		const double tSince = _tSince;
		const bool   attackSent = _attackSent, chaseSent = _chaseSent, capTried = _capBlinkTried;
		const int    blinks = _reachBlinks;
		ResetTargetState();
		_tSince = tSince;
		_attackSent = attackSent;
		_chaseSent = chaseSent;
		_capBlinkTried = capTried;
		_reachBlinks = blinks;
		Mem& m = M(c.id);
		m.blindT = -1;
		m.chainStage = -1;  // no resume at the push / blink stage
		_stuck.Reset();
		_stuckFor = 0;
	}

	// s7.5 fix lane 3 (brain.defendReReach; s7.5 gate BLOCKER, run 123733 HF3 12:40:30-41: the CZ turret FF0010E7 fighting the
	// player, 'NAV stage 2->3 d2Wait, 3->3 blinkWait, 3->4 blinkCap' - every landing spot in the player's view -, then 'brain
	// give up FF0010E7 unreach', the no-idle fallback held it at stage 4 with move=X vDes=0 for 1.5 s, it left the kept list
	// and was never picked again in 35 s; the installed build destroyed it in 2.3 / 4.5 s). A foe attacking the player is
	// never ignored: the old movement starts over on it - a fresh reach chain (navmesh first, fresh progress / stuck / blind
	// clocks, the per-target blink count kept) - and its blink stage may land in view indoors from now on (the D2 defend
	// blink, BLINK stage below). At most defendReReachMax per defendReReachWin s on one foe, then the old give-up (so another
	// foe gets a turn; the no-idle fallback still restarts it rather than holding him still).
	void Brain::RestartReach(const Cand& c)
	{
		const double tSince = _tSince, heldT = _heldT;
		const bool   attackSent = _attackSent, chaseSent = _chaseSent, capTried = _capBlinkTried;
		const int    blinks = _reachBlinks;
		ResetTargetState();
		_tSince = tSince;
		_heldT = heldT;  // the no-idle rotation keeps counting (noIdleHold)
		_attackSent = attackSent;
		_chaseSent = chaseSent;
		_capBlinkTried = capTried;
		_reachBlinks = blinks;
		Mem& m = M(c.id);
		m.blindT = -1;
		m.chainStage = -1;  // no resume at the push / blink stage
		_stuck.Reset();
		_stuckFor = 0;
	}
	// s12 fix r3 lane 3 (brain.defendCapBlink): DefendReReach's conditions without its restart - an attacker of the player, no other
	// attacker of the player (or, Tele 1, foe fighting the group) pickable, fewer than defendReReachMax re-reaches in the window
	bool Brain::CapDefendOk(const Cand& c, const Facts& f, const Params& p)
	{
		if (p.defendReReach < 0.5 || f.ext.on || !c.attackingPlayer) return false;
		for (std::size_t k = 0; k < f.cands.size() && k < _info.size(); ++k) {
			const Cand& o = f.cands[k];
			const Info& in = _info[k];
			if (o.id != c.id && (o.attackingPlayer || (TeleOn(f) && o.combatWithGroup)) && o.hostile && !o.killPending && in.pickable && !in.excluded && !in.essOut) return false;
		}
		const Mem& m = M(c.id);
		const bool fresh = _now - m.reReachSince > p.defendReReachWin || _now < m.reReachSince;
		return fresh || m.reReaches < static_cast<int>(p.defendReReachMax);
	}
	bool Brain::DefendReReach(const Cand& c, const Facts& f, const Params& p, Out& out)
	{
		if (p.defendReReach < 0.5 || f.ext.on || !c.attackingPlayer) return false;
		// another attacker of the player he can take now: the old give-up, so the pick goes to it (this one is reached
		// again when it is the only one left)
		for (std::size_t k = 0; k < f.cands.size() && k < _info.size(); ++k) {
			const Cand& o = f.cands[k];
			const Info& in = _info[k];
			// (R6 step 10, Facts::tele: a foe fighting the group counts too - ARCH 3.9.5 "band G / C foes he can reach are fought
			// while every band-D foe is out of reach", the same rule as the shield's TeleOther; no NAV -> BLINK -> shieldOther loop)
			if (o.id != c.id && (o.attackingPlayer || (TeleOn(f) && o.combatWithGroup)) && o.hostile && !o.killPending && in.pickable && !in.excluded && !in.essOut) return false;
		}
		Mem& m = M(c.id);
		if (_now - m.reReachSince > p.defendReReachWin || _now < m.reReachSince) {
			m.reReachSince = _now;
			m.reReaches = 0;
		}
		if (m.reReaches >= static_cast<int>(p.defendReReachMax)) return false;
		++m.reReaches;
		++_st.defendReReaches;
		out.giveUp = GiveUp::kUnreach;  // the give-up's log / bookmark line stays (as ExtReachFail); nothing is ignored
		out.giveUpId = c.id;
		RestartReach(c);
		_reReachId = c.id;
		_reReachUntil = _now + p.defendReReachWin;
		return true;
	}

	// R6 step 8.1 (brain.protectLock, core/protect.h): the protect guard over Pick's result. Views of the current target, the
	// pick and the best valid hurter (seen first, then nearest to him; essentials left to their own rules; fix lane 2: with
	// protectHurterFirst bit 4 + protectRushT a foe rushing the player first); sp::protect::Decide
	// says which one he takes. Only valid candidates take part (pickable, not excluded, a tier, not kill-pending, loaded, same
	// space): every give-up, leash, rest / cap, spent and hard veto of the old brain still ends a kept target.
	// s10 fix lane 2 (brain.protectStallYield / hurtChainMem; s10 gate item 2). A foe's kept chain memory says the navmesh / push
	// did not get him to it: the chain reached the push / blink stage on it within huntIgnore s and it stands where it stood.
	bool Brain::ChainRemembered(const Params& p, const Cand& c, const Mem& m) const
	{
		return p.chainMemory >= 0.5 && m.chainStage >= 2 && _now >= m.chainAt - kTiny && _now - m.chainAt <= p.huntIgnore + kTiny &&
		       Length(Flat(c.feet - m.chainFeet)) <= p.chainMemMove;
	}
	// His reach on the CURRENT target is stalled: the BLINK stage waiting (d2Wait / blinkWait / nearPush: a blink refused or
	// without a spot, pushing meanwhile), the chain's end hold (stage 4), or the push / door stage without real progress for
	// stallNoHit s. Never the shield hover ([R6] Tele 1: a held line), never a straight approach (a foe in the open that is merely
	// hard to hit: TABS lands its first hit ~1.2 s after arriving; the no-hit hand-over puts a foe behind something on the chain).
	bool Brain::ReachStalled(const Params& p, const Cand& c) const
	{
		if (c.id == 0 || c.id != _target || (_teleShieldId != 0 && _teleShieldId == c.id)) return false;
		const std::string_view why(_stageWhy ? _stageWhy : "");
		const double           t = std::max(0.25, p.stallNoHit);
		if (_stage >= 4 && _stage != kRouteStage) return true;
		if (_stage == 3 && (why == "d2Wait" || why == "blinkWait" || why == "nearPush")) return true;
		return _stage == 2 && _now - std::max(_stageAt, _pushRealProgAt) >= t - kTiny;
	}

	// FINISH A2 fix lane 5 (brain.turnedGuardR; finish\A2_lane5.md): a companion / ex-teammate that TURNED on the player - Cand::turned
	// now, or the brain saw it set within turnedGuardMem s (his knock-down takes its combat target away and with it the scan's turned
	// flag: in game 'ck=0' while it lay 174-817 u from the player and got up 7-9 s later to hit it again) - that is a hostile, loaded
	// candidate in his space, not kill-pending, within turnedGuardR of the player (standing or lying), indoors, the D2 brain moving him
	// (not the Warden, not [R6] Tele: their own jump rules). A memory from a clock that went back is no memory.
	bool Brain::TurnedGuard(const Facts& f, const Params& p, const Cand& c, const Info& in) const
	{
		if (!(p.turnedGuardR > 0) || f.ext.on || TeleOn(f) || !f.interiorCell || in.m == nullptr) return false;
		if (!c.hostile || c.killPending || !c.loaded || !c.sameSpace) return false;
		bool turned = c.turned;
		if (!turned && p.turnedGuardMem > 0) {
			const double at = in.m->turnedAt;
			turned = at > -1e8 && _now >= at - kTiny && _now - at <= p.turnedGuardMem + kTiny;
		}
		return turned && std::isfinite(in.dPlayer) && in.dPlayer <= p.turnedGuardR + kTiny;
	}

	// FINISH A2 fix lane 5 (brain.turnedGuardR / turnedGuardGap; the xp_x1b Saugus interior, finA gate tours finAG3 11:57:20-28.6,
	// fA-x1b 09:35:36.6-38.1, fA-x1b2 11:28:11.6-15.3): his reach on a HIDDEN TurnedGuard target is the D2 defend jump at once, from any
	// stage of the chain. In all three tours he re-picked the knocked-down ex-teammate 1.3-7 s before it got up, while the chain went NAV
	// (a 6000-8300 u route by the far stairs, away from the player) -> door push -> 'd2Wait' (the 2-jumps-a-10-minutes budget spent;
	// the blink stage's defend exception waits for a HIT within d2DbHitWin) -> give-up, and the jump came only after its next hits.
	// Kept from the BLINK stage: navBlink / d2DefendBlink on, the per-target count (navBlinksPer), the gap (turnedGuardGap; 0 = the
	// defend gap), the repeat rule with its defend exception (blinkRepeatT / blinkRepeatDefend), a seen take-off only within
	// navBlinkSeenMax, and a landing (the leg's own floor points, then BlinkSpot) with a clear sight AND path line to its chest - in the
	// player's view if need be (a defend case). Lifted: the budget (d2BlinkBudget), the near rule (navBlinkMin -> navBlinkNear) and the
	// no-blink-to-a-foe-that-fights-nobody rule (blinkIdleNo: it lies there, it gets up and attacks). One landing search per target every
	// kGuardRetry s; no landing / a refusal = nothing changes (the chain runs this frame as before). Pure brain logic, no engine call.
	bool Brain::TurnedGuardBlink(const Facts& f, const Params& p, IWorld& w, Out& out, const Cand& c, const Info& in)
	{
		constexpr double kGuardRetry = 0.25;
		if (!TurnedGuard(f, p, c, in)) return false;
		if (p.navBlink < 0.5 || p.d2DefendBlink < 0.5) return false;
		if (in.m->vis == 1) return false;  // seen: the straight flight (or the chain's own blocked-sight rules) owns it
		if (_reachBlinks >= static_cast<int>(p.navBlinksPer)) return false;
		const double gap = p.turnedGuardGap > 0 ? p.turnedGuardGap : std::min(p.navBlinkGap, p.d2DefendBlinkGap);
		if (_now - _lastBlinkAt < gap - kTiny) return false;
		if (_guardTryId == c.id && _now >= _guardTryAt - kTiny && _now - _guardTryAt < kGuardRetry - kTiny) return false;
		const double d = Length(c.chest - f.meChest);
		if (!(d >= p.navBlinkNear - kTiny)) return false;  // (NaN-safe: a non-finite distance never jumps)
		_guardTryId = c.id;
		_guardTryAt = _now;
		Mem&         rbm = M(c.id);
		const double hpR = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
		const bool   repeatWin = p.blinkRepeatT > 0 && _now >= rbm.reachBlinkAt - kTiny && _now - rbm.reachBlinkAt < p.blinkRepeatT - kTiny && !(hpR < p.lowHealth);
		const bool   lastBlinkHit = rbm.hitByMeAt >= rbm.reachBlinkAt - kTiny && rbm.hitByMeAt - rbm.reachBlinkAt <= p.blinkRepeatOkT + kTiny;
		if (repeatWin && !(p.blinkRepeatDefend >= 0.5 && lastBlinkHit)) {
			++_st.turnGuardRepeatNo;
			return false;
		}
		++_st.turnGuardTries;
		if (PlayerSees(f, p, w, f.meChest) && !(p.navBlinkSeen >= 0.5 && d <= p.navBlinkSeenMax + kTiny)) {
			++_st.turnGuardSeenNo;
			return false;
		}
		_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
		const double chestUp = f.meChest.z - f.meFeet.z;
		auto         pathOk = [&](const Vec3& a_chest) {  // (the BLINK stage's DPREP-FOLLOW path line, brain.blinkNeedPath)
            if (p.blinkNeedPath < 0.5) return true;
            if (!CanCast(1)) return false;
            const Vec3 to = c.chest - Normalize(c.chest - a_chest, { 1, 0, 0 }) * std::min(30.0, 0.5 * Length(c.chest - a_chest));
            if (RClear(w, a_chest, to)) return true;
            ++_st.blinkPathRejects;
            return false;
		};
		Vec3 spot{};
		bool have = false;
		if (_nav.forId == c.id) {
			for (int k = static_cast<int>(_nav.floor.size()) - 1; k >= 0 && !have && CanCast(2); --k) {
				const double dd = Length(Flat(_nav.floor[k] - c.feet));
				if (dd < p.navBlinkNear || dd > p.navBlinkFar) continue;
				const Vec3 chest = _nav.floor[k] + Vec3{ 0, 0, chestUp };
				if (RClearTo(w, chest, c.chest, c.id, p.sightReach) && pathOk(chest)) {
					spot = _nav.floor[k];
					have = true;
				}
			}
		}
		if (!have) {
			int r = 0;
			have = BlinkSpot(c, f.meFeet, p, w, spot, r, std::max(0, _rayCap - _rays));
			_rays += r;
			if (have) have = pathOk(spot + Vec3{ 0, 0, chestUp });
		}
		if (!have || !sp::math::IsFinite(spot)) {
			++_st.turnGuardNoSpot;
			return false;
		}
		// the jump: the BLINK stage's own bookkeeping (gap, D2 budget ring, per-target count, the repeat clock), a DEFEND blink
		out.blink = true;
		out.blinkFeet = spot;
		out.blinkWhy = 3;
		out.blinkDefend = true;
		++_st.d2DefendBlinks;
		++_st.turnGuardBlinks;
		_st.turnGuardLastId = c.id;
		_st.turnGuardLastD = d;
		_st.turnGuardLastDpl = in.dPlayer;
		_st.turnGuardLastAgo = c.turned ? 0.0 : _now - in.m->turnedAt;
		_lastBlinkAt = _now;
		D2NoteBlink();
		++_reachBlinks;
		++_st.reachBlinks;
		rbm.reachBlinkAt = _now;
		_nav = NavState{};  // fresh plan after the jump (usually in sight now)
		_triedNav = _doorAi = false;
		_stage = 0;
		_stageAt = _now;
		_stageWhy = "turnGuard";
		out.move = Move::kHold;
		out.vDes = {};
		out.aim = f.meChest;
		out.accel = p.flyAccel;
		out.vMax = 0;
		return true;
	}

	// FINISH lane 6 (brain.bigTurnGroup): a big monster in combat with the GROUP (not the player) that has just come - a candidate for at
	// most protectBigTurnT s, not one known before - within bigAimR of the player and protectBigTurnMeR of him (Pick's bigAim and the
	// guard's View use the same test). 0 = never
	bool Brain::TurnGroupBig(const Params& p, const Cand& c, const Info& in) const
	{
		// FINISH A2 lane 3 (brain.bigTurnRun 1; the finA gate's Swan rule R2 failures, finish\A2_lane3.md): "just turned" is the start of
		// its current run of fighting the group or attacking the player (Mem::grpSince: the TURN), not its first frame as a candidate -
		// a behemoth that stood passive in its pond for minutes and then wakes up is exactly the case - and the radius round the player
		// is brain.bigTurnGroupR (0 = bigAimR): 76 recorded Swan wake-ups fighting HIM 1500-2500 u from the player since 10-19 (16 with
		// its rock on the player 5-10 s later, all but one taken > 1 s late) vs 9 at 2500-3000 u (no rock on the player). Lane 6's
		// newcomer path is kept as it was. 0 = lane 6 exactly.
		// The TURN path acts only while nobody hurts the player (_plHurtNow: a hit on the player within s2fHitWin s) - the gate's failures were
		// all quiet ones (Swan never hit the player); with gunners shooting the player the desktop PR-91 burst arms took more gunner hits for
		// no fewer boulders, so there the order is lane 6's - and (round 2) only while no attacker of the player stands nearer to HIM
		if (p.bigTurnRun >= 0.5) {
			// round 2 (the verifier of round 1): as lane 6, only a big monster fighting HIM / a teammate - NOT one attacking the player.
			// Round 1 let a just-turned attacker of the player in too (out to bigTurnGroupR from the player and protectBigTurnMeR 3500 u from
			// him, past bigAimMeR): as the CURRENT target such a monster is judged by bigAim's cur branch (1.25 x bigAimMeR = 3125 u), so
			// one 3125-3500 u from him could be picked and dropped. An attacker of the player keeps its own rules (bigAim within bigAimR /
			// bigAimMeR, the guard's BigTurned within protectBigTurnMeR - finA: the 6 ck=1 wake-ups were taken 0.00-0.93 s); a monster
			// fighting him / a teammate is asked here again every frame as the current target too (bigAim: not 'cur' for it), the same radii.
			if (!(p.bigTurnGroup >= 0.5) || c.cls != Cls::kBig || c.essentialOnly || c.attackingPlayer || !c.inCombat || !c.combatWithGroup || !in.m) return false;
			const double dMe = _curF ? Length(c.chest - _curF->meChest) : 0.0;
			if (p.protectBigTurnMeR > 0 && _curF && dMe > p.protectBigTurnMeR + kTiny) return false;
			const bool newcomer = in.m->firstAt > -1e8 && !in.m->knownOld && _now - in.m->firstAt <= p.protectBigTurnT + kTiny && in.dPlayer <= p.bigAimR + kTiny;  // (lane 6's rule as it was)
			if (newcomer) return true;
			const double r = p.bigTurnGroupR > 0 ? p.bigTurnGroupR : p.bigAimR;
			if (!(in.m->grpSince > -1e8 && _now >= in.m->grpSince - kTiny && _now - in.m->grpSince <= p.protectBigTurnT + kTiny && in.dPlayer <= r + kTiny)) return false;
			// it waits while somebody hurts the player (a hit within s2fHitWin s by a foe still alive: the gate's failures were all quiet
			// ones; with gunners shooting the player the s81 PR-91 burst arms took more hits for no fewer boulders) ...
			if (_plHurtNow) {
				++_bigTurnHurtN;
				return false;
			}
			// ... and while an attacker of the player (its combat target the player, pickable, alive) stands nearer to HIM than the big
			// monster: the closest-attacker rule - he finishes the attacker he reaches first; the big monster nearer to him than every
			// attacker of the player is taken at its turn (soak 120152: Swan 986 u from him, the raiders aiming at the player 2134-2536 u)
			if (_curF && (c.id == _atkMe1Id ? _atkMe2 : _atkMe1) < dMe - kTiny) {
				++_bigTurnNearN;
				return false;
			}
			// finB (finA2 REVIEW 2, the integrator's soak K3 god-back dt1 20:25:09-21: he left FF001280 - 1616 u from him, RUNNING AT the
			// player - for the just-woken Swan 1166 u from him that never hit the player, 1.00 -> 0.25): an attacker of the player that is
			// close to the player (kAtkNearPlR) or running at it holds the wake-up too, whatever its distance to HIM (the user's order: the
			// attacker closest to the player before a big monster fighting only him). Far aimers that do not come (PR-91 'game': 1300-1900 u
			// from the player, never hitting) still let him take the behemoth at its wake-up, as lane 3 wanted
			if (p.bigTurnNearPl >= 0.5 && _curF && _atkNearPl) {
				++_bigTurnNearN;
				return false;
			}
			return true;
		}
		if (!(p.bigTurnGroup >= 0.5) || c.cls != Cls::kBig || c.essentialOnly || c.attackingPlayer || !c.inCombat || !c.combatWithGroup || !in.m) return false;
		if (!(in.m->firstAt > -1e8) || in.m->knownOld || _now - in.m->firstAt > p.protectBigTurnT + kTiny || in.dPlayer > p.bigAimR + kTiny) return false;
		return !(p.protectBigTurnMeR > 0) || !_curF || Length(c.chest - _curF->meChest) <= p.protectBigTurnMeR + kTiny;
	}

	int Brain::ProtectFilter(const Facts& f, const Params& p, int a_cur, int a_pick)
	{
		const auto& cs = f.cands;
		const int   n = static_cast<int>(std::min(cs.size(), _info.size()));
		const auto  view = [&](int k) {
			sp::protect::View v;
			if (k < 0 || k >= n) return v;
			const Cand& c = cs[k];
			const Info& in = _info[k];
			v.id = c.id;
			v.idx = k;
			v.valid = in.m && in.pickable && !in.excluded && in.tier != Tier::kNone && !c.killPending && c.hostile && c.loaded && c.sameSpace;
			v.lastHitPlayerAt = c.lastHitPlayerAt;
			// FINISH lane 6 (brain.bigTurnGroup): the guard sees a just-come big monster fighting the GROUP as one aiming at the player, as
			// the brain's band-D big rule does (Pick bigAim) - else R1 keeps a current hurter against it ('no threat') while its rock comes
			const bool turnGrp = TurnGroupBig(p, c, in);
			v.aimsAtPlayer = c.attackingPlayer || turnGrp;
			v.dPlayer = in.dPlayer;
			// s12 fix lane 3 (brain.protectBigClosing): a big creature not in combat yet that runs at the player (the Concord deathclaw
			// out of its manhole) is a big monster in the fight too (ThreatRank 2 when seen within protectBigR of the player)
			const double runAt = Dot(Flat(c.vel), FlatDir(f.playerFeet - c.feet));
			const bool   bigRun = p.protectBigClosing >= 0.5 && c.hostile && runAt >= sp::protect::kRushMinClose && runAt <= sp::protect::kRushMaxClose;
			v.big = c.cls == Cls::kBig && !c.essentialOnly && (c.inCombat || c.combatWithGroup || bigRun);
			v.bigCls = c.cls == Cls::kBig && !c.essentialOnly;  // s12 fix lane 3 (brain.protectBigFinishR)
			v.underwater = c.underwater;
			v.seen = in.m && in.m->vis == 1;
			v.dMe = Length(c.chest - f.meChest);
			v.essentialOnly = c.essentialOnly;
			v.closing = Dot(Flat(c.vel), FlatDir(f.playerFeet - c.feet));  // fix lane 2 (brain.protectRushT): its own run at the player
			v.explosive = c.explosive;                                      // s11 fix lane 1 (brain.protectBomberR)
			v.fightsGroup = c.combatWithGroup && c.id == _bigAimHoldId;     // s12 fix lane 2 (brain.bigAimR: the current target's bar, one that aimed at the player)
			v.age = in.m && in.m->firstAt > -1e8 ? _now - in.m->firstAt : 1e9;  // s12 fix round 3 lane 2 (R7 bigFirstPick: a newcomer)
			if (in.m && in.m->knownOld && _burst) v.age = 1e9;  // completion side 3 s13r3 (brain.newcomerKnown): in a burst it is no newcomer
			v.prevHitPlayerAt = in.m ? in.m->plHit[1] : -1e9;               // s12 r3 lane 4 (R7b: "hit the player only once")
			v.otherFight = c.inCombat && c.combatWithGroup && !c.attackingPlayer;  // completion lane 1 (BigFor: busy with him / a teammate)
			v.melee = c.meleeArmed;  // completion side lane 4 (brain.protectRushMelee: the rush rank for a melee foe only)
			v.atkAge = (c.attackingPlayer && in.m && in.m->atkSince > -1e8) ? _now - in.m->atkSince :
			           (turnGrp ? _now - (p.bigTurnRun >= 0.5 ? in.m->grpSince : in.m->firstAt) : 1e9);  // explore m1 lane 2 (brain.protectBigTurnMeR; FINISH lane 6: a group turn counts from its coming; A2 lane 3 bigTurnRun: from the turn)
			// explore cycle m1 fix lane 1 (brain.protectFirst): a suicider in a fight (in combat, attacking the player or fighting the
			// group: a dormant one far off is the ordinary rules'), a turned companion / ex-teammate
			v.suicider = c.suicider && (c.inCombat || c.attackingPlayer || c.combatWithGroup);
			v.turned = c.turned;
			v.hidKnown = in.m && in.m->vis == 0;  // explore cycle m2 fix lane 3 (brain.protectBombHidFix: known hidden, not merely not looked at yet)
			// finish A2 (Fable, brain.protectBombSwapHid): s since a sight trace last saw it (Mem::visTrueAt); 0 while seen or never looked at
			// finB (finA2 REVIEW 1): a thrower KNOWN hidden that his sight never saw counts as unseen since it became a candidate (was 0 = "seen
			// just now": BombSwapHidden never swapped it for one nearer the player, BombCloseKept held it as the only choice); never looked at = 0
			if (!in.m || in.m->vis == 1) v.unseenFor = 0.0;
			else if (in.m->visTrueAt > -1e8) v.unseenFor = std::max(0.0, _now - in.m->visTrueAt);
			else v.unseenFor = (p.bombUnseenFirst >= 0.5 && in.m->vis == 0 && in.m->firstAt > -1e8) ? std::max(0.0, _now - in.m->firstAt) : 0.0;
			v.withGroup = c.combatWithGroup || c.attackingPlayer;  // finish A fix lane 2 (brain.protectFirstNarrow)
			// s12 r2 lane 4 (RC, brain.protectCommit): his reach on the CURRENT target is going on - closer by progressStep (or nav
			// progress) within protectCommitProg s while not stalled, or stalled (ReachStalled: the blink stage waiting, the push without
			// progress, the chain's end hold) for less than protectCommitProg s (in game 060211 the reach on the hidden synth went
			// door -> blinkWait 0.42 s -> blinkCap -> re-reach -> blinkWait 0.34 s -> blink -> hit); last frame's clocks
			if (k == a_cur && c.id == _target && (p.protectCommit > 0 || p.protectCloserGuard > 0)) {  // (completion lane 1: R5's progress too)
				// s12 r3 lane 5 (brain.protectCommitWall): the same window on the wall clock (Facts::wall) when the glue gives one - the
				// brain clock ran 2.3-2.7x real time in the sc3 cell, so 0.5 s there was 0.19-0.25 s of the real blink wait
				const bool wallOn = p.protectCommitWall >= 0.5 && std::isfinite(f.wall) && f.wall >= 0.0;
				if (wallOn && (_rcProgSeen != _progAt || !(_rcProgW > -1e8))) {
					_rcProgSeen = _progAt;  // (set to the brain's now when he got closer: last frame's, at the latest)
					_rcProgW = f.wall - std::clamp(_now - _progAt, 0.0, 1e6);
				}
				const bool stalled = ReachStalled(p, c);
				if (!stalled) _rcStallId = 0;
				else if (_rcStallId != c.id) {
					_rcStallId = c.id;
					_rcStallSince = _now;
					_rcStallSinceW = f.wall;
				}
				bool wallHeld = false;
				v.reachOk = sp::protect::CommitReachOk(stalled, p.protectCommitProg, _now, _rcStallSince, _progAt, wallOn, f.wall, _rcStallSinceW, _rcProgW, &wallHeld);
				if (wallHeld) ++_rcWallKeeps;
			}
			return v;
		};
		const sp::protect::View vCur = view(a_cur), vNew = view(a_pick);
		sp::protect::View       vBest;
		double                  bestR = 1e300;
		// completion side 2 s13r2 (brain.protectRushGuard bit 1): the best HURTER as the rush-off guard ranks it (HurterRank, no rusher
		// first) - Decide's competitor while a thrower he sees is the current target or the pick
		sp::protect::View vBestH;
		double            bestHR = 1e300;
		// fix lane 2 (protectHurterFirst bit 4 + protectRushT): a foe rushing the player competes too (BestCand / BestRank)
		const bool rushBest = (_protCfg.hurterFirst & 4) && _protCfg.rushT > 0;
		for (int k = 0; k < n; ++k) {
			if (k == a_cur) continue;
			const Cand& c = cs[k];
			if (c.essentialOnly || (!(rushBest && c.attackingPlayer) && (c.lastHitPlayerAt < -1e8 || _now - c.lastHitPlayerAt > _protCfg.hurtWin + kTiny))) continue;  // cheap pre-check
			const sp::protect::View v = view(k);
			if (_protCfg.rushGuard > 0 && v.valid && v.id != 0 && !v.essentialOnly && sp::protect::Hurting(_protCfg, v, _now)) {  // side 2 s13r2
				const double rh = sp::protect::HurterRank(_protCfg, v);
				if (rh < bestHR) {
					bestHR = rh;
					vBestH = v;
				}
			}
			if (!sp::protect::BestCand(_protCfg, v, _now)) continue;
			const double r = sp::protect::BestRank(_protCfg, v);
			if (r < bestR) {
				bestR = r;
				vBest = v;
			}
		}
		// step 10 fix lane 1 (R5, protectBigAtkR / protectCloserR): the most urgent candidate for the player (the current target
		// judged with its own bar); cheap pre-check: aiming at the player and big, or near the player. A closer that HITS the
		// player from its side (within kCloserAt) counts even while it is on his give-up ignore list (GK3 dt2 17:18:34.4-36.9:
		// he gave up the raider FF0015D8 at a door, flew 4800 u to a far F2 foe, and the raider walked up and hit the player 2x
		// before the ignore ran out; the reason he gave up - he could not get to it - is gone when it stands at the player).
		// Never a spent one (flying from his punch), an essential at rest / capped, or one the pass loop excluded this frame
		sp::protect::View vUrg;
		if (_protCfg.bigAtkR > 0 || _protCfg.closerR > 0) {
			sp::protect::NoteUrgent(_protCfg, _prot, a_cur >= 0 ? &vCur : nullptr, _now);  // the current target's looser bar (a real closer)
			// completion lane 1 (brain.protectCloserGuard; s10 gate GK3 01:07:01.595: R5 held him 5.2 s on the closer FF00189F he could
			// not reach - move=H, noProg - while the seen FF00189D it took him off shot the player -0.39): the urgent target R5 holds
			// that his reach makes no progress on (View::reachOk: not closer within protectCommitProg s, or a longer stall) is given
			// back to the rules and R5 leaves it alone kUrgBlock s
			if (_protCfg.closerGuard > 0 && a_cur >= 0 && vCur.id != 0 && vCur.id == _prot.urgentId && !vCur.reachOk) {
				if (_prot.urgBlockId != vCur.id || _now - _prot.urgBlockAt > sp::protect::kUrgBlock) ++_prot.urgNoProg;
				_prot.urgBlockId = vCur.id;
				_prot.urgBlockAt = _now;
				_prot.urgentId = 0;
			}
			int        bestU = 0;
			bool       bestCur = false;
			const auto nearR = sp::protect::kCloserKeep * _protCfg.closerR;
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				if (!(c.attackingPlayer || TurnGroupBig(p, c, _info[k])) || c.essentialOnly) continue;  // (FINISH lane 6: + the just-come big monster fighting the group)
				if (_protCfg.closerGuard > 0 && c.id == _prot.urgBlockId && _now >= _prot.urgBlockAt - kTiny && _now - _prot.urgBlockAt <= sp::protect::kUrgBlock + kTiny) continue;
				if (!((_protCfg.bigAtkR > 0 && c.cls == Cls::kBig && _info[k].dPlayer <= _protCfg.bigAtkR) || _info[k].dPlayer <= std::max(nearR, sp::protect::kCloserAt))) continue;
				const bool        isCur = k == a_cur;
				sp::protect::View v = isCur ? vCur : view(k);
				const Info&       in = _info[k];
				if (!v.valid && _protCfg.closerR > 0 && in.m && !in.pickable && !in.essOut && !in.excluded && in.tier != Tier::kNone && in.m->ignoreUntil > _now &&
					!(in.m->spentUntil > _now) && !c.killPending && c.hostile && c.loaded && c.sameSpace && in.dPlayer <= sp::protect::kCloserAt) {
					v.valid = true;
					if (!sp::protect::Hurting(_protCfg, v, _now)) v.valid = false;
					else ++_st.protUrgentIgnored;  // (frames)
				}
				const int u = sp::protect::Urgency(_protCfg, v, _now, isCur && c.id == _prot.urgentId);  // (the looser bar: only the target R5 holds)
				if (u > 0 && (bestU == 0 || sp::protect::UrgentBetter(u, v, isCur, bestU, vUrg, bestCur))) {
					bestU = u;
					bestCur = isCur;
					vUrg = v;
				}
			}
		}
		// s11 fix lane 1 (R6b, protectBomberR; the s11 gate's soak death 1): the best foe with an explosive / missile weapon aiming at
		// the player near the player (the current target judged with its looser bar / hold); cheap pre-check: explosive and aiming
		sp::protect::View vBomb;
		// s13 r3 fix lane 2 (brain.protectBomberAimR, core/protect.h BomberAimFar): does a valid foe hurt the player AT the player this
		// frame (CloseHurter)? A far thrower aiming at the player counts by its throw range only while none does. Cheap pre-check: a hit on
		// the player within hurtWin, within max(nearR, kCloserAt) of it
		_protCfg.plHurtClose = false;
		if (_protCfg.bomberR > 0 && _protCfg.bomberAimR > 0 && _protCfg.bomberGuard > 0) {
			const double closeR = std::max(_protCfg.nearR, sp::protect::kCloserAt) + kTiny;
			for (int k = 0; k < n && !_protCfg.plHurtClose; ++k) {
				const Cand& c = cs[k];
				if (c.essentialOnly || c.lastHitPlayerAt < -1e8 || _now - c.lastHitPlayerAt > _protCfg.hurtWin + kTiny || _info[k].dPlayer > closeR) continue;
				_protCfg.plHurtClose = sp::protect::CloseHurter(_protCfg, k == a_cur ? vCur : view(k), _now);
			}
		}
		// completion side 4 s13r3 (brain.protectBomberAimHurtT, core/protect.h BomberAimHurtHeld): the nearest foe to the player that is no
		// thrower (no explosive weapon: a thrower is the thrower rule's own case) and hit the player within protectBomberAimHurtT s - the
		// aim range never counts a thrower farther from the player than it (the s13r3 arm soak rr dt3 03:12:08-19: a gunner 732 u from the
		// player shot it 5 times while he chased molotov raiders 2100-2250 u away). Cheap pre-check: a hit on the player within the window
		_protCfg.plHurtNearD = 1e9;
		if (_protCfg.bomberR > 0 && _protCfg.bomberAimR > 0 && _protCfg.bomberGuard > 0 && _protCfg.bomberAimHurtT > 0) {
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				if (c.essentialOnly || c.explosive || c.lastHitPlayerAt < -1e8 || _now < c.lastHitPlayerAt - kTiny || _now - c.lastHitPlayerAt > _protCfg.bomberAimHurtT + kTiny) continue;
				if (_info[k].dPlayer >= _protCfg.plHurtNearD) continue;
				const sp::protect::View v = k == a_cur ? vCur : view(k);
				if (v.valid && v.id != 0 && !v.essentialOnly) _protCfg.plHurtNearD = v.dPlayer;
			}
		}
		// explore cycle m1 fix lane 2 (brain.protectBomberAtkR, core/protect.h BomberAtkHeld): does a valid foe that is no thrower attack the
		// player within protectBomberAtkR of it this frame (AtkNearPlayer: aims at it - an essential only while it hurts it - or hurts it)?
		// Then a thrower he cannot see and cannot reach now never takes him (Corvega xp6 09:17:35-38: hidden throwers through doors while
		// the raiders at the player hit it). Cheap pre-check: no explosive, aiming at / a hit on the player, within the range
		_protCfg.plAtkNear = false;
		if (_protCfg.bomberR > 0 && _protCfg.bomberAtkR > 0) {
			const double atkR = sp::protect::kCloserKeep * _protCfg.bomberAtkR + kTiny;
			for (int k = 0; k < n && !_protCfg.plAtkNear; ++k) {
				const Cand& c = cs[k];
				if (c.explosive || _info[k].dPlayer > atkR) continue;
				const bool hitNow = c.lastHitPlayerAt > -1e8 && _now >= c.lastHitPlayerAt - kTiny && _now - c.lastHitPlayerAt <= _protCfg.hurtWin + kTiny;
				if (!((c.attackingPlayer && !c.essentialOnly) || hitNow)) continue;
				const sp::protect::View x = k == a_cur ? vCur : view(k);
				// (explore cycle m2 fix lane 3, brain.protectBombHidFix: only an attacker he can reach now - seen, or within kBombShortR of him - makes a
				// hidden thrower wait; the xm1 gate cz 14:33:12-15.9: FD8 held 'bombHidHeld 115 -> 425' while the attacker he went for was hidden too)
				const bool near = sp::protect::AtkNearPlayer(_protCfg, x, _now, k == a_cur);
				const bool outR = near && _protCfg.bombHidFix > 0 && sp::protect::AtkOutOfReach(x);
				if (outR) ++_prot.bombHidOut;  // (frames x attackers)
				_protCfg.plAtkNear = near && !outR;
				if (_protCfg.plAtkNear) _plAtkNearId = c.id;  // FINISH B2 fix lane 4 (brain.protectBombHidLatch): who made it on
			}
			// FINISH B2 fix lane 4 (brain.protectBombHidLatch; the finB gate's Mass Fusion roof army, tour 043314 04:36:49.0-50.5: 'bomberWhy ...
			// plAtkNear=1' then, 1.5 s later, 'why=notThrower ... out=FF001591 seen=0 visAge=-1 ... plAtkNear=0' - the raider running at the player
			// went out of his sight / reach for single frames, the hold of the throwers he cannot see went off with it and the thrower rule took him
			// off that raider (seen, 493 u) to a thrower he had never seen; that raider then beat the player): the hold stays on protectBombHidLatch s
			// after it was last on by itself, while the attacker that made it on is still a live, hostile candidate. A thrower that HIT the player is
			// never held (protectBombHurtFirst, unchanged), a thrower he sees or can reach at once is never held (BombReachNow, unchanged)
			if (_protCfg.plAtkNear) {
				_plAtkNearAt = _now;
			} else if (p.protectBombHidLatch > 0 && _plAtkNearId != 0 && _plAtkNearAt > -1e8 && _now >= _plAtkNearAt - kTiny &&
			           _now - _plAtkNearAt <= std::min(0.5, p.protectBombHidLatch) + kTiny) {
				for (int k = 0; k < n; ++k) {
					const Cand& c = cs[k];
					if (c.id != _plAtkNearId) continue;
					const auto live = k == a_cur ? vCur : view(k);
					if (c.hostile && c.loaded && c.sameSpace && !c.killPending &&
					    sp::protect::AtkNearPlayer(_protCfg, live, _now, k == a_cur)) {
						_protCfg.plAtkNear = true;
						++_st.b2l4HidLatch;  // (frames)
					}
					break;
				}
			}
		}
		if (_protCfg.bomberR > 0) {
			bool bombCur = false;
			// s13 r2 fix lane 4 (brain.protectBomberGuard; the s13r1 gate GQ hf 1080u CZ.b 11:21:04.555-08.968 'keepBomber cur=FF001138' x19
			// while FF001138 stayed hidden with noProg 0.95 -> 2.96 s and the pick FF001141 beat the player): the current thrower his reach
			// makes no progress on (View::reachOk false: not closer within protectCommitProg s, or stalled longer - a blink wait) is no
			// longer the thrower rule's; it leaves that thrower alone kBombBlock s (the brain's own pick may still take it)
			const bool bombGuard = _protCfg.bomberGuard > 0;
			bool       curBomb = false;
			// finish A2 (Fable, brain.protectBombCloseKeep; the roof army death 11:30:14.6 / 17.2 - the current thrower 435 / 579 u from him, seen,
			// left for another thrower): the close current thrower he sees is the rule's only choice this frame - no no-progress stamp, no other
			// thrower weighed (core/protect.h BombCloseKept)
			const bool closeKept = a_cur >= 0 && vCur.id != 0 && vCur.id == _target && sp::protect::BombCloseKept(_protCfg, vCur, _now, &_prot);
			if (closeKept) ++_prot.bombCloseKeeps;  // (frames)
			if (!closeKept && bombGuard && a_cur >= 0 && vCur.id != 0 && vCur.id == _target && vCur.explosive && (p.protectCommit > 0 || p.protectCloserGuard > 0) && !vCur.reachOk &&
			    sp::protect::Bomber(_protCfg, vCur, _now, true, &_prot)) {
				if (_prot.bombBlockId != vCur.id || _now - _prot.bombBlockAt > sp::protect::kBombBlock) ++_prot.bombNoProg;
				_prot.bombBlockId = vCur.id;
				_prot.bombBlockAt = _now;
				if (_protCfg.bomberCommit > 0) sp::protect::BombRelNote(_prot, vCur.id, _now);  // s13 r3 fix lane 1: remembered per thrower
			}
			// s13 r3 fix lane 1 (brain.protectBomberCommit; the s13r2 gate GK3 dt1 ping-pong, core/protect.h BombCommitSkip): the current
			// target is itself a thrower by the rule's own bar (whatever its no-progress drop) - no thrower he dropped within
			// protectBomberCommitT s takes him off it
			const bool curThrower = bombGuard && _protCfg.bomberCommit > 0 && a_cur >= 0 && vCur.id != 0 && vCur.id == _target && vCur.explosive && !vCur.essentialOnly &&
			                        sp::protect::Bomber(_protCfg, vCur, _now, true, &_prot);
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				if (!c.explosive || c.essentialOnly) continue;
				const bool              isCur = k == a_cur;
				const sp::protect::View v = isCur ? vCur : view(k);
				if (!isCur && _info[k].m && sp::protect::BombReconsiderHit(_protCfg, _prot, v, _now, _info[k].m->bombReconsiderHitAt, &vCur))
					++_st.bombFreshReconsiders;
				if (closeKept && !isCur) continue;  // finish A2 (Fable, protectBombCloseKeep): the close current thrower is the only choice
				if (bombGuard && c.id == _prot.bombBlockId && _now >= _prot.bombBlockAt - kTiny && _now - _prot.bombBlockAt <= sp::protect::kBombBlock + kTiny) continue;
				if (sp::protect::BombCommitSkip(_protCfg, _prot, c.id, isCur, curThrower, _now)) {  // s13 r3 fix lane 1 (frames)
					++_prot.bombCommitSkips;
					continue;
				}
				if (bombGuard && !sp::protect::BomberNearMe(_protCfg, v, isCur)) {  // beyond protectBomberMeR of him (frames)
					++_prot.bombFar;
					// completion side 4 s13r3: a far thrower the aim range would count but a nearer foe that shot the player holds back (frames)
					if (sp::protect::BomberAimHurtHeld(_protCfg, v) && sp::protect::BomberAimFarRaw(_protCfg, v, isCur)) ++_prot.bombAimHurt;
					continue;
				}
				if (!sp::protect::Bomber(_protCfg, v, _now, isCur, &_prot)) continue;
				// explore m1 lane 2 (protectBomberAtkR): a thrower he does not see, beyond kBombShortR of him, while a foe attacks the player
				// near it is no thrower first (BomberAtkHeld 2) - left out here, so a thrower he can reach is still chosen (frames x throwers)
				// (finish A fix lane 2, brain.protectBombHurtFirst: never one that hit the player within protectHurtWin - REVIEW m1 item 3)
				if (_protCfg.plAtkNear && !sp::protect::BombReachNow(v)) {
					if (_protCfg.bombHurtFirst > 0 && sp::protect::Hurting(_protCfg, v, _now)) {
						++_prot.bombHurtKeeps;  // (frames x throwers)
					} else {
						++_prot.bombHidHeld;
						continue;
					}
				}
				// s13 r3 fix lane 2: a thrower only by its aim range (beyond protectBomberMeR of him and of the player) - frames x throwers
				if (bombGuard && _protCfg.bomberMeR > 0 && sp::protect::BomberAimFar(_protCfg, v, isCur)) {
					const double lim = (isCur ? sp::protect::kCloserKeep : 1.0) * _protCfg.bomberMeR + kTiny;
					if (v.dMe > lim && v.dPlayer > lim) ++_prot.bombAimFar;
				}
				// completion lane 1 (bomberFirst): not a thrower the stall yield took him off within huntIgnore s (he cannot get to
				// it now; no A -> B -> A between the stall yield and this rule)
				if (_protCfg.bomberFirst > 0 && _info[k].m && _now >= _info[k].m->stallLeftAt - kTiny && _now - _info[k].m->stallLeftAt <= p.huntIgnore + kTiny) {
					++_prot.bombStallSkip;
					continue;
				}
				if (isCur) curBomb = true;
				// finish A2 (Fable, protectBombSwapHid): a swap the finA bar would have made (the current thrower unseen this frame, this one
				// kBombSwapPad u nearer the player) that the sight hysteresis refuses - frames x throwers
				if (!isCur && curBomb && _protCfg.bombSwapHid > 0 && !vCur.seen && !sp::protect::BombSwapHidden(_protCfg, vCur) && v.dPlayer + sp::protect::kBombSwapPad < vCur.dPlayer)
					++_prot.bombSwapHidKeeps;
				if (vBomb.id == 0 || sp::protect::BomberBetter(_protCfg, v, isCur, vBomb, bombCur)) {  // (s13 r2 fix lane 4: + the nearer-thrower swap)
					vBomb = v;
					bombCur = isCur;
				}
			}
			if (curBomb && !bombCur && vBomb.id != 0) ++_prot.bombSwaps;  // s13 r2 fix lane 4: a hidden current thrower left for a nearer one
			// finish A2 lane 1 round 2 (DIAGNOSTIC only - no decision reads it; SPN|PROT|bomberWhy when the thrower rule switches him): why the
			// current target is not the loop's choice this frame - the loop's own tests above re-asked in the same order for the current target
			// alone (pure, and only on a frame whose choice is another thrower) - and both foes' facts (Brain::BombWhy)
			_bwPend = {};
			if (vBomb.id != 0 && !(a_cur >= 0 && vCur.id == vBomb.id)) {
				const auto side = [&](const sp::protect::View& a_v, BombWhy::Side& a_s) {
					a_s.seen = a_v.seen;
					a_s.aims = a_v.aimsAtPlayer;
					a_s.reachOk = a_v.reachOk;
					a_s.dMe = static_cast<float>(a_v.dMe);
					a_s.dPl = static_cast<float>(a_v.dPlayer);
					a_s.hitAgo = a_v.lastHitPlayerAt > -1e8 && _now >= a_v.lastHitPlayerAt ? static_cast<float>(_now - a_v.lastHitPlayerAt) : -1.0f;
					for (int k = 0; k < n; ++k)
						if (cs[k].id == a_v.id) {
							const Mem* m = _info[k].m;
							a_s.visAge = m && m->visTrueAt > -1e8 && _now >= m->visTrueAt ? static_cast<float>(_now - m->visTrueAt) : -1.0f;
							break;
						}
				};
				_bwPend.out = vBomb.id;
				_bwPend.plAtkNear = _protCfg.plAtkNear;
				side(vBomb, _bwPend.o);
				if (a_cur >= 0 && vCur.id != 0) {
					const Cand&  cc = cs[a_cur];
					std::uint8_t why = BombWhy::kOther;
					if (curBomb) why = BombWhy::kSwap;
					else if (!cc.explosive || cc.essentialOnly) why = BombWhy::kNotThrower;
					else if (bombGuard && cc.id == _prot.bombBlockId && _now >= _prot.bombBlockAt - kTiny && _now - _prot.bombBlockAt <= sp::protect::kBombBlock + kTiny) why = BombWhy::kNoProg;
					else if (bombGuard && !sp::protect::BomberNearMe(_protCfg, vCur, true)) why = BombWhy::kFarMe;
					else if (!sp::protect::Bomber(_protCfg, vCur, _now, true, &_prot)) why = BombWhy::kNoAim;
					else if (_protCfg.plAtkNear && !sp::protect::BombReachNow(vCur) && !(_protCfg.bombHurtFirst > 0 && sp::protect::Hurting(_protCfg, vCur, _now))) why = BombWhy::kHidHold;
					else if (_protCfg.bomberFirst > 0 && _info[a_cur].m && _now >= _info[a_cur].m->stallLeftAt - kTiny && _now - _info[a_cur].m->stallLeftAt <= p.huntIgnore + kTiny)
						why = BombWhy::kStall;
					_bwPend.cur = vCur.id;
					_bwPend.curWhy = why;
					side(vCur, _bwPend.c);
				}
			}
			// completion lane 1 (bomberFirst; "a big monster hitting the player outranks a far thrower"): no thrower first while a big
			// monster comes for the player (BigFor) nearer to the player than the thrower, or hitting it - the big rules decide then
			if (_protCfg.bomberFirst > 0 && vBomb.id != 0) {
				for (int k = 0; k < n; ++k) {
					const Cand& c = cs[k];
					if (c.cls != Cls::kBig || c.essentialOnly || c.id == vBomb.id) continue;
					const sp::protect::View v = k == a_cur ? vCur : view(k);
					// (explore m1 lane 2: + a big monster that has just turned on the player, R5-urgent by protectBigTurnMeR - BigTurned)
					if (!sp::protect::BigFor(_protCfg, v, _now, k == a_cur) &&
					    !(sp::protect::BigTurned(_protCfg, v) && _protCfg.bigAtkR > 0 && v.dPlayer <= _protCfg.bigAtkR + kTiny))
						continue;
					if (v.dPlayer <= vBomb.dPlayer + kTiny || sp::protect::Hurting(_protCfg, v, _now)) {
						++_prot.bombBigYield;
						vBomb = {};
						break;
					}
				}
			}
		}
		// s12 fix round 2 lane 3 (R7, brain.protectBigFirst; the s12r1 gate r3m Concord deathclaw, core/protect.h BigFirst): the big
		// monster coming for the player (the current target with its looser bar first, then the pick, then the nearest to the
		// player) and the number of valid foes hurting the player (R7 leaves only a single hidden ranged hurter for it); cheap
		// pre-check: hurting the player or cls B
		sp::protect::View vBig;
		int               nHurt = 0;
		if (_protCfg.on && _protCfg.bigFirst > 0) {
			sp::protect::NoteBigFirst(_protCfg, _prot, a_cur >= 0 ? &vCur : nullptr, a_cur >= 0 && a_cur == a_pick, _now);  // the current target's hold clock
			int bigRank = 99;  // 0 the current target, 1 the pick, 2 any other
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				const bool  hurtCand = !c.essentialOnly && c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= _protCfg.hurtWin + kTiny;
				if (!hurtCand && c.cls != Cls::kBig) continue;
				const bool              isCur = k == a_cur, isPick = k == a_pick;
				const sp::protect::View v = isCur ? vCur : isPick ? vNew : view(k);
				if (hurtCand && sp::protect::Hurting(_protCfg, v, _now)) ++nHurt;
				if (c.cls != Cls::kBig || !sp::protect::BigFirst(_protCfg, _prot, v, _now, isCur, isPick)) continue;
				const int r = isCur ? 0 : isPick ? 1 : 2;
				if (r < bigRank || (r == bigRank && v.dPlayer < vBig.dPlayer)) {
					bigRank = r;
					vBig = v;
				}
			}
		}
		// explore cycle m1 fix lane 1 (brain.protectFirst, core/protect.h FirstRank): the best first foe - a suicider in a fight near the
		// player, then a turned companion / ex-teammate attacking it (the current one with its looser bar; FirstBetter). Cheap pre-check:
		// the scan's suicider / turned flags (none in almost every frame)
		sp::protect::View vFirst;
		if (_protCfg.on && _protCfg.first > 0) {
			int  bestFR = 0;
			bool bestFCur = false;
			// finish A fix lane 2 (brain.protectFirstNarrow; verifier round 1: with the old "unless within protectTurnedNearR of the player" a stalled
			// first foe near the player was first again the very next frame - ~1 s stalled on it, 1 frame on the hurter, back, repeating): a first foe
			// the stall yield took him off stays out of the first rank while the target he took instead is a hurter he can reach (valid, hurting the
			// player, his reach on it not stalled) and huntIgnore s have not passed - the same bar as the stall yield's own keep below. When that
			// hurter is down, stops hurting or stalls too, the first foe is first again at once
			const bool curReachHurter = _protCfg.firstNarrow > 0 && a_cur >= 0 && a_cur < n && vCur.valid && vCur.id != 0 && sp::protect::Hurting(_protCfg, vCur, _now) &&
			                            !ReachStalled(p, cs[a_cur]);
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				if (!c.suicider && !c.turned) continue;
				const bool              isCur = k == a_cur;
				const sp::protect::View v = isCur ? vCur : k == a_pick ? vNew : view(k);
				const int               r = sp::protect::FirstRank(_protCfg, v, _now, isCur);
				if (r <= 0) continue;
				// (finish A fix lane 2: the stall yield's first foe - see curReachHurter above)
				if (curReachHurter && !isCur && _info[k].m && _now >= _info[k].m->stallLeftAt - kTiny && _now - _info[k].m->stallLeftAt <= p.huntIgnore + kTiny) {
					++_prot.firstStallSkips;  // (frames)
					continue;
				}
				if (bestFR == 0 || sp::protect::FirstBetter(r, v, isCur, bestFR, vFirst, bestFCur)) {
					bestFR = r;
					bestFCur = isCur;
					vFirst = v;
				}
			}
			if (bestFR > 0) {
				++_st.firstFrames;
				if (bestFR >= 2) ++_st.firstSuicFrames;
			}
		}
		const sp::protect::View* out = nullptr;
		bool                     dropKept = false;  // finish A fix lane 2 (protectBombDropKeep: Decide says when the drop keep decided)
		// (s10f merge: fix lane 1's R5 urgent candidate goes into Decide; fix lane 2's stall yield / keep runs after it)
		auto verdict = sp::protect::Decide(_protCfg, _prot, _now, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr,
			vUrg.id ? &vUrg : nullptr, vBomb.id ? &vBomb : nullptr, vBig.id ? &vBig : nullptr, nHurt, out, vBestH.id ? &vBestH : nullptr, _protCfg.rushGuard > 0,  // (side 2 s13r2)
			vFirst.id ? &vFirst : nullptr, &dropKept);  // (explore cycle m1 fix lane 1; finish A fix lane 2)
		const bool firstHeld = verdict == sp::protect::Verdict::kFirst || verdict == sp::protect::Verdict::kKeepFirst;  // (no stall yield / R7b off a first foe)
		// finish A fix lane 2 (protectBombDropKeep's kBombDropKeepT cap): stamp the keep run on this thrower (a gap of kBombDropGap s starts a new run);
		// count a run the cap ended (he leaves the thrower the keep held)
		if (dropKept && a_cur >= 0 && vCur.id != 0) {
			if (_prot.dropKeepId != vCur.id || _now < _prot.dropKeepLast - kTiny || _now - _prot.dropKeepLast > sp::protect::kBombDropGap + kTiny) {
				_prot.dropKeepId = vCur.id;
				_prot.dropKeepSince = _now;
			}
			_prot.dropKeepLast = _now;
			++_prot.bombDropKeeps;  // (frames)
			if (out && out->id == vCur.id && _protCfg.bombHurtKeep > 0 && vBomb.id != 0 && vBomb.id != vCur.id) ++_prot.bombHurtKeepHolds;  // finish A2 (Fable): the thrower branch (frames)
		} else if (a_cur >= 0 && vCur.id != 0 && out && out->id != vCur.id && sp::protect::BombDropCapped(_prot, vCur.id, _now)) {
			++_prot.bombDropCaps;  // (episodes)
			_prot.dropKeepId = 0;
		}
		// s13 r2 fix lane 4 (brain.protectBomberGuard): frames the guard kept him off the thrower for a foe hurting the player at the
		// player - Decide's own answer, re-asked for the counter
		if (_protCfg.bomberGuard > 0 && vBomb.id != 0 && (!out || out->id != vBomb.id)) {
			const sp::protect::View* rules = nullptr;
			(void)sp::protect::DecideRules(_protCfg, _prot, _now, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr, rules);
			if (sp::protect::BomberHeld(_protCfg, _now, a_cur >= 0 ? &vCur : nullptr, rules, vBomb) != 0) ++_prot.bombHeldClose;
			// FINISH B2 fix lane 5 (brain.protectBomberFarAtk): frames only the attacker-at-the-player clause held the far thrower back
			// (BomberTooFar would not have with the key 0) - Decide's own answer re-asked on the rules' choice
			if (_protCfg.bomberFarAtk > 0 && rules && !(vBomb.dMe > _protCfg.bomberFarR + kTiny && rules->dMe <= _protCfg.bomberCurMeR + kTiny) &&
			    sp::protect::BomberTooFar(_protCfg, vBomb, rules, _now, a_cur >= 0 && rules->id == vCur.id))
				++_st.bombFarAtk;
			// explore m1 lane 2 (protectBomberAtkR): frames the near attacker (1) / the switch gap (3) held the thrower back - Decide's own
			// answer re-asked on the rules' choice
			if (_protCfg.bomberAtkR > 0) {
				const int ah = sp::protect::BomberAtkHeld(_protCfg, _prot, _now, a_cur >= 0 ? &vCur : nullptr, rules, vBomb);
				if (ah == 1 || ah == 2) ++_prot.bombAtkHeld;
				else if (ah == 3) ++_prot.bombSwGap;
			}
		}
		// s13 r3 fix lane 1 (brain.protectBomberCommit): frames the guard's commit hold kept him on the current thrower (BombCommitHold: the
		// rules' choice was a thrower he dropped for no progress; the thrower rule itself keeps the current one only as its own vBomb)
		if (_protCfg.bomberCommit > 0 && verdict == sp::protect::Verdict::kKeepBomber && out && a_cur >= 0 && out->id == vCur.id && vBomb.id != vCur.id) ++_prot.bombCommitHolds;
		// s13 r3 fix lane 1 (brain.protectBomberFarNear, REVIEW_r2 1): switches to the thrower near the player that the s13r2 far rule
		// (BomberTooFar without the player clause) would have refused for the near attacker - the rules' choice, re-asked for the counter
		// (only on a switch: no extra work on the frames he stays on it)
		if (_protCfg.bomberFarNear > 0 && _protCfg.bomberFarR > 0 && vBomb.id != 0 && out && out->id == vBomb.id && verdict == sp::protect::Verdict::kBomber) {
			const sp::protect::View* rules = nullptr;
			(void)sp::protect::DecideRules(_protCfg, _prot, _now, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr, rules);
			if (rules && sp::protect::BomberTooFarRaw(_protCfg, vBomb, rules, _now)) ++_prot.bombFarNear;
		}
		// s13 r3 fix lane 2 (brain.protectBomberKeep): frames the current thrower was kept over a farther foe hurting the player at the
		// player (BomberKeepNearer) - the same question with the key 0
		if (_protCfg.bomberKeep > 0 && _protCfg.bomberGuard > 0 && vBomb.id != 0 && out && out->id == vBomb.id && a_cur >= 0 && vCur.id == vBomb.id) {
			sp::protect::Cfg c0 = _protCfg;
			c0.bomberKeep = 0;
			const sp::protect::View* rules = nullptr;
			(void)sp::protect::DecideRules(_protCfg, _prot, _now, &vCur, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr, rules);
			if (sp::protect::BomberHeld(c0, _now, &vCur, rules, vBomb) != 0) ++_prot.bombKeepNear;
		}
		// finish A2 lane 1 round 2 (diagnostic): the thrower rule switched him this frame - keep its why for SPN|PROT|bomberWhy
		if (verdict == sp::protect::Verdict::kBomber && out && _bwPend.out != 0 && out->id == _bwPend.out) {
			const std::uint64_t k = _bombWhy.n + 1;
			_bombWhy = _bwPend;
			_bombWhy.n = k;
			_bombWhy.at = _now;
		}
		_bwPend = {};
		// explore m1 fix lane 3 (brain.protectCloserAtR): R5 switches only its at-the-player clause made (Preemptable said no) - re-asked
		// for the counter, only on such a switch
		if (_protCfg.closerAtR > 0 && verdict == sp::protect::Verdict::kUrgent && out && vUrg.id != 0 && out->id == vUrg.id) {
			const sp::protect::View* rules = nullptr;
			(void)sp::protect::DecideRules(_protCfg, _prot, _now, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr, rules);
			if (rules && !sp::protect::Preemptable(_protCfg, *rules, _now) && sp::protect::CloserAtPre(_protCfg, vUrg, *rules, _now)) ++_st.m1CloserAt;
		}
		// s10 fix lane 2 (brain.protectStallYield; s10 gate item 2, "never hold a stall stage while another attacker is hitting
		// the player; switch to one he can reach"): the guard keeps the current target, but his reach on it is stalled (the
		// blink stage waiting, the push without progress, the chain's end hold) - the best valid HURTER whose chain he is not
		// stalled on (no kept push / blink memory at its spot) is taken instead (seen first, then nearest; labelled "hurter")
		sp::protect::View vAlt;
		// finish A fix lane 2 (brain.protectFirstNarrow, REVIEW m1 item 1): the stall yield also acts on a first foe his reach has been stalled
		// on for kFirstStallT s (a suicider on a quarry ledge, a turned ally behind a locked door) while another foe hurts the player
		constexpr double kFirstStallT = 1.0;
		const bool       firstStall = firstHeld && _protCfg.firstNarrow > 0 && a_cur >= 0 && out && out->idx == a_cur && _rcStallId != 0 && _rcStallId == vCur.id &&
		                        _now >= _rcStallSince - kTiny && _now - _rcStallSince >= kFirstStallT - kTiny;
		const std::uint64_t yields0 = _st.stallYields;
		if (p.protectStallYield >= 0.5 && _protCfg.on && (!firstHeld || firstStall) && a_cur >= 0 && a_cur < n && out && out->idx == a_cur && ReachStalled(p, cs[a_cur])) {
			const bool curHurts = sp::protect::Hurting(_protCfg, vCur, _now);
			double     altR = 1e300;
			for (int k = 0; k < n; ++k) {
				if (k == a_cur) continue;
				const Cand& c = cs[k];
				if (c.essentialOnly || c.lastHitPlayerAt < -1e8 || _now - c.lastHitPlayerAt > _protCfg.hurtWin + kTiny) continue;  // hurting the player
				const sp::protect::View v = view(k);
				if (!sp::protect::Hurting(_protCfg, v, _now) || v.essentialOnly) continue;
				if (sp::protect::Flip(_protCfg, _prot, &v, _now)) continue;  // the one he just left: no ping-pong (flipBack)
				if (sp::protect::BombCommitHold(_protCfg, _prot, _now, &vCur, &v)) continue;  // s13 r3 fix lane 1: nor a thrower dropped for no progress (protectBomberCommit)
				// explore cycle m2 fix lane 3 (brain.protectLeaveHitT; Corvega m2b 16:33:15.592 'PROT|hurter cur=FF0012E3 -> FF0013FB', 0.23 s after
				// FF0012E3's hit): a stalled current target that holds is left by this rule only for a hurter he SEES (the reason of the yield: one he can
				// reach) or one better for the player (LeaveHeld)
				if (!v.seen && sp::protect::LeaveHeld(_protCfg, vCur, v, _now)) continue;
				// (a stalled current HURTER is left only for one he is not known to be stalled on - no kept chain memory at its spot,
				// not left by this rule within huntIgnore s; a stalled non-hurter for any hurter)
				if (curHurts && _info[k].m &&
				    (ChainRemembered(p, c, *_info[k].m) || (_now >= _info[k].m->stallLeftAt && _now - _info[k].m->stallLeftAt <= p.huntIgnore + kTiny)))
					continue;
				const double r = sp::protect::HurterRank(_protCfg, v);
				if (r < altR) {
					altR = r;
					vAlt = v;
				}
			}
			// s12 r2 lane 4 (RC): a committed current hurter in a SHORT stall (a blink wait: View::reachOk) is not left for a hurter that
			// is not clearly better (CommitHolds) - after protectCommitProg s of stall the yield acts as before
			if (vAlt.id != 0 && sp::protect::Commit(_protCfg, vCur, _now) && sp::protect::CommitHolds(_protCfg, _prot, vCur, vAlt, _now)) {
				vAlt = {};
				++_rcStallKeeps;
				verdict = sp::protect::Verdict::kKeepCommit;
			}
			if (vAlt.id != 0) {
				out = &vAlt;
				verdict = sp::protect::Verdict::kHurter;
				++_st.stallYields;
				if (_info[a_cur].m) _info[a_cur].m->stallLeftAt = _now;  // not taken back by this rule while the other one stalls
			}
		} else if (p.protectStallYield >= 0.5 && _protCfg.on && !firstHeld && a_cur >= 0 && a_cur < n && out && out->idx >= 0 && out->idx < n && out->idx != a_cur && _info[out->idx].m &&
		           _now >= _info[out->idx].m->stallLeftAt && _now - _info[out->idx].m->stallLeftAt <= p.huntIgnore + kTiny && vCur.valid &&
		           sp::protect::Hurting(_protCfg, vCur, _now) && !ReachStalled(p, cs[a_cur])) {
			// ... and a target he left by this rule is not taken back (the flip-back time over, the pick nearer) while the hurter he
			// took instead is still being reached (not stalled): no A -> B -> A cycle between a stalled and a reachable hurter
			out = &vCur;
			verdict = sp::protect::Verdict::kKeepThreat;
			++_st.stallKeeps;
		}
		if (firstStall && _st.stallYields != yields0) ++_prot.firstYields;  // finish A fix lane 2: a stalled first foe left for a hurter
		// s12 r3 lane 4 (R7b, brain.protectBigOnce; the s12r2 gate's Concord r5 F11 1.19 / 0.74 s, core/protect.h BigOnce): the big
		// monster coming for the player (the current target with its looser bar first, then the pick, then the nearest to the player)
		// over a final choice that hit the player at most once; the counts: valid foes hurting the player (hurtWin) and sustained
		// hurters (two hits within protectBigOnceMem, hurting now). Cheap pre-check: cls B, or a hit on the player within the memory
		sp::protect::View vOnce;
		if (_protCfg.on && _protCfg.bigOnce > 0 && !firstHeld) {  // (explore cycle m1 fix lane 1: never off a first foe)
			sp::protect::NoteBigOnce(_protCfg, _prot, a_cur >= 0 ? &vCur : nullptr, _now);  // the current big target's hold clock
			int  rank = 99, nHurtB = 0, nManyB = 0;  // rank: 0 the current target, 1 the pick, 2 any other
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				const bool  hitMem = !c.essentialOnly && c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= std::max(_protCfg.hurtWin, _protCfg.bigOnceMem) + kTiny;
				if (!hitMem && c.cls != Cls::kBig) continue;
				const bool              isCur = k == a_cur, isPick = k == a_pick;
				const sp::protect::View v = isCur ? vCur : isPick ? vNew : view(k);
				if (hitMem && c.cls != Cls::kBig && sp::protect::Hurting(_protCfg, v, _now)) {  // (the big monsters themselves are not counted)
					++nHurtB;
					if (sp::protect::OnceHits(_protCfg, v, _now) > 1) ++nManyB;
				}
				if (c.cls != Cls::kBig || !sp::protect::BigComing(_protCfg, _prot, v, _now, isCur)) continue;
				const int r = isCur ? 0 : isPick ? 1 : 2;
				if (r < rank || (r == rank && v.dPlayer < vOnce.dPlayer)) {
					rank = r;
					vOnce = v;
				}
			}
			if (vOnce.id != 0) {
				const auto vb = sp::protect::BigOnce(_protCfg, _now, a_cur >= 0 ? &vCur : nullptr, &vOnce, nHurtB, nManyB, out);
				if (vb != sp::protect::Verdict::kNew) verdict = vb;
			}
		}
		// explore m1 lane 2: R5 switches only CloserHidden made (the same frame with the key 0 keeps the rules' choice), and frames R5 holds
		// a big monster only its turn makes urgent (BigTurned beyond protectBigAtkMeR of him)
		if (verdict == sp::protect::Verdict::kUrgent && _protCfg.closerHidden > 0 && out && vUrg.id != 0 && out->id == vUrg.id) {
			sp::protect::Cfg         c0 = _protCfg;
			const sp::protect::View* o0 = nullptr;
			c0.closerHidden = 0;
			(void)sp::protect::Decide(c0, _prot, _now, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, vBest.id ? &vBest : nullptr, &vUrg, vBomb.id ? &vBomb : nullptr,
				vBig.id ? &vBig : nullptr, nHurt, o0, vBestH.id ? &vBestH : nullptr, _protCfg.rushGuard > 0);
			if (!o0 || o0->id != vUrg.id) ++_prot.closerHid;
		}
		if ((verdict == sp::protect::Verdict::kUrgent || verdict == sp::protect::Verdict::kKeepUrgent) && out && _protCfg.bigTurnMeR > 0 && out->big &&
		    sp::protect::BigTurned(_protCfg, *out) && _protCfg.bigAtkMeR > 0 && out->dMe > _protCfg.bigAtkMeR + kTiny)
			++_prot.bigTurn;
		// FINISH lane A_combat_zone (brain.czEssHurtR; the explore cycle m2 Combat Zone cage, run 20261025-164534-xm2c-m2third-x1 16:46:28.1-36.7,
		// 0.71, 6 of 7 hits from foes he was not on: 'PROT|bomber -> FF00100E' - a PROTECTED molotov raider he could not see, 561 u away, aiming
		// at the player, never hitting it - then keepBomber / keepThreat held it 29.5-34.3 s (door -> blinkWait -> blinkCap, noProg 3.7 s)
		// while the ESSENTIAL melee raider FF001012 beat the player (31.33, 34.74) and the essential thrower FF000FEE hit it (32.69, 36.20).
		// The guard never offers an essential (BestCand / R4 skip essentialOnly) and its holds keep a hidden non-hurter against the pick):
		// an essential that hit the player within protectHurtWin s, within czEssHurtR u of the player (the nearest to the player; a resting /
		// capped / held-down / spent one is no valid view - the knock-down rules stay the brain's), is taken over a final choice he does NOT
		// see that is not hurting the player - never over a first foe, a big monster, a seen foe or a hurter. Cheap pre-check: the final
		// choice first, then essential-only candidates with a hit on the player within hurtWin
		// Round 2 (the verifier): (1) never over a THROWER the bomber rule holds - the verdict kBomber / kKeepBomber (its pre-emptive pick,
		// its keep, finish lane A_hurter_thrower's protectBombDropKeep keep) or this frame's thrower vBomb: the user's rule is throwers
		// first, and a hidden thrower that has not thrown yet is exactly the CZ.a shape (1.00 -> 0.27); a thrower the rule has let go
		// (keepThreat / kNew on a hidden one that never hit - the m2c cage 31.406 on) is still left for the essential; (2) czEssHoldT:
		// the cage bars let a seen frame of the other choice through every 1-3 s - while he stays on the essential the rule took (the run:
		// every frame he is on it, a gap > 0.25 s ends it), the other choice's sight no longer takes him back for czEssHoldT s from the
		// take (every other condition stays: the essential still hurting, the other choice still not hurting the player, no first foe /
		// big / thrower); a take after he was off it starts a new run
		const bool onEss = _czEssId != 0 && a_cur >= 0 && vCur.id == _czEssId && _now >= _czEssLast - kTiny && _now - _czEssLast <= 0.25 + kTiny;
		if (onEss) _czEssLast = _now;
		sp::protect::View vEssH;
		const bool        essHoldOn = p.czEssHoldT > 0 && onEss && _now - _czEssSince < p.czEssHoldT - kTiny;
		const bool        bombHeld = verdict == sp::protect::Verdict::kBomber || verdict == sp::protect::Verdict::kKeepBomber || (out && vBomb.id != 0 && out->id == vBomb.id);
		if (p.czEssHurtR > 0 && _protCfg.on && !firstHeld && out && out->valid && out->id != 0 && !out->essentialOnly && (!out->seen || essHoldOn) && !out->big && !out->bigCls &&
		    !sp::protect::Hurting(_protCfg, *out, _now) && sp::protect::FirstRank(_protCfg, *out, _now) <= 0) {
			for (int k = 0; k < n; ++k) {
				const Cand& c = cs[k];
				if (!c.essentialOnly || c.lastHitPlayerAt < -1e8 || _now < c.lastHitPlayerAt - kTiny || _now - c.lastHitPlayerAt > _protCfg.hurtWin + kTiny) continue;
				if (_info[k].dPlayer > p.czEssHurtR + kTiny || (vEssH.id != 0 && _info[k].dPlayer >= vEssH.dPlayer)) continue;
				const sp::protect::View v = k == a_cur ? vCur : k == a_pick ? vNew : view(k);
				if (v.valid && v.id != 0 && v.id != out->id && sp::protect::Hurting(_protCfg, v, _now)) vEssH = v;
			}
			// (the held run keeps its own essential while it is still a valid hurter - not a nearer one that just hit)
			if (essHoldOn && vEssH.id != 0 && vEssH.id != _czEssId) {
				const sp::protect::View vh = vCur;
				if (vh.valid && vh.essentialOnly && vh.lastHitPlayerAt > -1e8 && _now - vh.lastHitPlayerAt <= _protCfg.hurtWin + kTiny && vh.dPlayer <= p.czEssHurtR + kTiny &&
				    sp::protect::Hurting(_protCfg, vh, _now))
					vEssH = vh;
			}
			if (vEssH.id != 0 && bombHeld) {
				vEssH = {};
				++_st.czEssThrow;
			}
			if (vEssH.id != 0) {
				if (out->seen) ++_st.czEssHold;  // (only the hold let it act this frame)
				if (!(onEss && vEssH.id == _czEssId)) _czEssSince = _now;  // (a fresh take: he was not on it)
				_czEssId = vEssH.id;
				_czEssLast = _now;
				out = &vEssH;
				verdict = sp::protect::Verdict::kHurter;  // (logged as a 'PROT|hurter' switch; counted in czEssHurt)
				++_st.czEssHurt;
			}
		}
		sp::protect::Note(_prot, verdict, a_cur >= 0 ? &vCur : nullptr, a_pick >= 0 ? &vNew : nullptr, out, _now);
		sp::protect::NoteBomberKeep(_protCfg, _prot, out, _now);  // s13 r3 fix lane 2 (brain.protectBomberKeep: a thrower any rule took keeps the hold)
		sp::protect::NoteBombAtk(_protCfg, _prot, verdict, a_cur >= 0 ? &vCur : nullptr, out, _now);  // explore m1 lane 2 (the thrower switch gap)
		return out ? out->idx : -1;
	}

	// s4f (brain.extStageFail): a reach failure NOTE for the Warden (ETA penalty, the near-failure rule, the defend jump with
	// warden.cnlOnceHitting) without the give-up's chain reset: the old movement goes on with its blink stage (push, valve)
	void Brain::ExtReachNote(const Cand& c, const char* a_why, Out& out)
	{
		const double gap = _curF ? std::max(0.0, _curF->ext.failGap) : 1.0;
		if (_extFailId != c.id || _now - _extFailAt >= gap - kTiny || _now < _extFailAt) {
			out.reachFail = true;
			out.reachFailId = c.id;
			out.reachFailWhy = a_why;
			_extFailId = c.id;
			_extFailAt = _now;
			++_st.extFails;
			++_st.extStageFails;
		}
	}

	bool Brain::DefendJump(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out)
	{
		// ARCH §4.11 defend jump: the Warden granted it (band D, it hit the player within 2 s, jump only or the close-range law
		// failed twice, the shield was held 1 s and the player was hit again - below 35 % at once -, within the budget). View
		// rule: both ends out of the player's eye and camera view at once; in view only while the player is below
		// tele.defendInViewHp (jumpInViewOk); else he keeps shielding and tries again in 0.1 s. Landing: a floor spot of the
		// current navmesh leg 120-500 u from it with a clear line to its chest, else BlinkSpot (85 u away / side / -side /
		// -away), and a clear PATH line to its chest. The target is kept (brain.blinkKeep) and attacked at once.
		_lastBlinkTryAt = _now;
		_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
		const bool inViewOk = f.ext.on ? f.ext.jumpInViewOk : (f.tele.on && f.tele.jumpInViewOk);  // R6 step 10: the Pilot's permit
		if (!inViewOk && PlayerSees(f, p, w, f.meChest)) return false;  // a seen take-off
		const Vec3 up{ 0, 0, std::max(0.0, f.meChest.z - f.meFeet.z) };
		auto       pathOk = [&](const Vec3& a_chest) {
            if (!CanCast(1)) return false;
            const Vec3 to = c.chest - Normalize(c.chest - a_chest, { 1, 0, 0 }) * std::min(30.0, 0.5 * Length(c.chest - a_chest));
            return RClear(w, a_chest, to);
		};
		Vec3 spot{};
		bool have = false;
		// s10 fix lane 3 (gate item 6c, GT20 17:41:11.576: the Pilot's defend jump took a navmesh-leg point 769 u away that landed
		// him 586 u from the melee raider at the player - out of sight, 'NAV rayFail', the player dead 1.8 s later): with [R6] Tele
		// the landing is within tele.jumpLandMax u of its chest (punch range: the contact punch from the landing)
		const double landMax = (TeleOn(f) && f.tele.jumpLandMax > 0) ? f.tele.jumpLandMax : 1e18;
		if (_nav.forId == c.id) {
			for (int k = static_cast<int>(_nav.floor.size()) - 1; k >= 0 && !have && CanCast(2); --k) {
				const double dd = Length(Flat(_nav.floor[k] - c.feet));
				if (dd < p.navBlinkNear || dd > p.navBlinkFar) continue;
				if (Length(_nav.floor[k] + up - c.chest) > landMax) continue;  // s10 fix lane 3 (6c)
				const Vec3 chest = _nav.floor[k] + up;
				if (RClearTo(w, chest, c.chest, c.id, p.sightReach) && (inViewOk || !PlayerSees(f, p, w, chest)) && pathOk(chest)) {
					spot = _nav.floor[k];
					have = true;
				}
			}
		}
		if (!have) {
			int r = 0;
			have = BlinkSpot(c, f.meFeet, p, w, spot, r, std::max(0, std::min(8, _rayCap - _rays)));
			_rays += r;
			if (have && !inViewOk) have = !PlayerSees(f, p, w, spot + up);
			if (have) have = pathOk(spot + up);
		}
		if (have && Length(spot + up - c.chest) > landMax) have = false;  // s10 fix lane 3 (6c): never a landing out of punch range
		if (!have) return false;
		out.blink = true;
		out.blinkFeet = spot;
		out.blinkWhy = 5;
		// s4 fix lane 2 (REVIEW: DefendJump vs the runtime's DoBlink refusals): this is only the ASK. The jump is committed in
		// OnOwnBlink, which the runtime calls only after the warp really happened (brain_glue::DidBlink): the blink time, the
		// D2 blink budget, the defend-jump count and the fresh reach chain / progress clocks (ResetTargetState there). A jump
		// the runtime refuses (tele.jump off, the proxy controller, seen by the real camera, a live veto) changes nothing here:
		// the reach chain and its clocks go on and no budget is spent; the next ask waits blinkGap s (_djTryAt).
		_djPendId = c.id;
		_djPendAt = _now;
		_djTryAt = _now;
		out.move = Move::kHold;
		out.vDes = {};
		out.aim = f.meChest;
		out.accel = p.flyAccel;
		out.vMax = 0;
		out.dvMax = 0;
		out.mayAttack = false;
		out.directClear = false;
		return true;
	}

	void Brain::ShieldMove(const Facts& f, const Params& p, Out& out, int a_sel, bool a_vis)
	{
		// ARCH §3.9.5: band-D actors exist, none is reachable, nothing of band G / C can be taken: he hovers at the shield spot
		// (150 u from the player toward the nearest band-D shooter, on the player's floor) so his body blocks the line. Within
		// swing reach of the target he still punches it. The Warden ends the shield the frame any band-D foe is reachable.
		const Cand& c = f.cands[a_sel];
		const Info& in = _info[a_sel];
		_noPathChase = false;
		out.target = c.id;
		out.tier = in.tier;
		out.cls = c.cls;
		out.visible = a_vis;
		out.pin = in.pin;
		const double d = Length(c.chest - f.meChest);
		out.dist = d;
		out.face = FlatDir(c.feet - f.meFeet);
		out.mayAttack = a_vis && d <= p.swingRange && !f.vatsPlayback && !f.killMove;
		const Vec3   spotFeet = IsFinite(f.ext.shieldSpot) ? f.ext.shieldSpot : f.playerFeet;
		const Vec3   spotChest = spotFeet + (f.meChest - f.meFeet);
		const double vmax = f.indoors ? std::min(p.approachMax, p.indoorMaxSpeed) : p.approachMax;
		out.move = Move::kApproach;
		out.vDes = ApproachVDes(f.meFeet, f.meChest, spotFeet, spotChest, vmax, p, f.meVel);
		out.aim = spotChest;
		out.aimVel = f.playerVel;
		out.accel = p.flyAccel;
		out.vMax = vmax;
		out.planKind = Plan::kDirect;
		out.directClear = false;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		out.hunt = HuntState::kLocal;
		_aiChasing = false;
		_wasFarLeg = false;
		++_st.extShieldFrames;
	}

	// ------------------------------------------------------------------------------------------- R6 step 10: the Pilot's policy
	// ARCH §3.9.5 / §4.11 on the old movement (Facts::tele.on). See TeleExt in brain.h.
	namespace
	{
		// integrator (in game hf / soak: shield spots at (126,70,51), (139,55,15), (-100,111,21) - near the world origin - while the
		// player stood at (9410,4015); dSpot 10 000 u: Facts::playerChest (the scan's GetChestPos of the player) was stale in
		// those scenes): the shield uses the player's chest only when it sits over his feet, else feet + 90 u
		Vec3 TelePlayerChest(const Facts& f)
		{
			const Vec3   c = f.playerChest;
			const double dz = c.z - f.playerFeet.z;
			if (sp::math::IsFinite(c) && Length(Vec3{ c.x - f.playerFeet.x, c.y - f.playerFeet.y, 0 }) <= 80.0 && dz >= 20.0 && dz <= 200.0) return c;
			return f.playerFeet + Vec3{ 0, 0, 90 };
		}
	}
	bool Brain::TeleCase(const Cand& c, const Facts& f) const
	{
		// band D on the old brain: it hit the player within brain.defendWindow s or the player is its combat target (attackingPlayer)
		if (!(c.attackingPlayer && !c.killPending && c.loaded && c.sameSpace)) return false;
		// integrator (in game hf 12:45-12:48: "shields" of far-hunt foes 9 000-118 000 u from the shield spot - the spot is next to
		// the player - with hitP -1: he flew to the player for tele.shieldRetry s and back to the foe, again and again): a foe farther
		// than tele.shieldMaxD from the player that has not hit it within tele.defendHitWindow s is no shield case (the old refusal:
		// he pushes at it, then the next foe)
		if (f.tele.shieldMaxD > 0 && Length(c.chest - TelePlayerChest(f)) > f.tele.shieldMaxD && !(c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= f.tele.hitWindow + kTiny))
			return false;
		// integrator (in game sc5 12:59:28-40, the Concord speakeasy: a raider hidden behind a door that never hit the player was
		// "shielded" 4 x 4 s next to the player - the chain reached its BLINK stage after 0.6 s of door work - and died only 20 s
		// later; cz 12:32-12:33: 40 s of shield on a caged raider with hitP -1): the shield is for an attacker that HURTS the
		// player (ARCH 4.11 "a sealed-off shooter keeps hurting the player"): it hit the player within tele.shieldHitAgo s - or,
		// while he already shields it, hit HIM (his body takes its shots). Anyone else at the BLINK stage: the old refusal (he
		// pushes at it, then the chain ends - the way through is tried again). 0 = every attacker of the player (CORE)
		if (f.tele.shieldHitAgo > 0) {
			const bool hitP = c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= f.tele.shieldHitAgo + kTiny;
			const bool hitMe = _teleShieldId == c.id && c.lastHitHimAt > -1e8 && _now - c.lastHitHimAt <= f.tele.shieldHitAgo + kTiny;
			if (!hitP && !hitMe) return false;
		}
		return true;
	}

	bool Brain::TeleOther(const Cand& c, const Facts& f) const
	{
		// ARCH §3.9.5: "if band-G / C foes are reachable while every band-D foe is not, he fights them and the shield is not
		// used" - on the old brain: another candidate that fights the group (attacks the player, him or a teammate), is
		// pickable and SEEN now (his eyes reach it: the reachable proxy of the old brain)
		for (std::size_t k = 0; k < f.cands.size() && k < _info.size(); ++k) {
			const Cand& o = f.cands[k];
			const Info& in = _info[k];
			if (o.id == c.id || !(o.attackingPlayer || o.combatWithGroup) || !o.hostile || o.killPending || !o.loaded || !o.sameSpace) continue;
			if (!in.m || !in.pickable || in.excluded || in.essOut || in.tier == Tier::kNone) continue;
			if (in.m->vis == 1) return true;
		}
		return false;
	}

	bool Brain::TeleShield(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out)
	{
		if (!TeleCase(c, f)) {
			if (_teleShieldId == c.id) _teleShieldId = 0;
			return false;
		}
		if (_teleShieldId != c.id) {
			_teleShieldId = c.id;
			_teleShieldSince = _now;
			++_st.teleShields;
		}
		// ARCH 3.5 (C6: "none only after every planner, re-tried every 2 s"): a shield that held tele.shieldRetry s without a jump
		// permit hands the foe back to a FRESH reach chain (navmesh first, from where he hovers now); it comes back here if the
		// foe is still out of reach. Never while the jump is permitted (the shield clock it earned is kept).
		if (f.tele.shieldRetry > 0 && _now - _teleShieldSince >= f.tele.shieldRetry - kTiny && f.tele.jumpId != c.id) {
			++_st.teleShieldRetries;
			RestartReach(c);  // stage -1, fresh nav / progress / stuck clocks (_teleShieldId 0)
			// the caller's chain loop goes on at NAV this frame (as ReachHidden's own start does)
			_triedNav = _triedRay = _doorAi = _aiTried = false;
			if (f.interiorCell) _triedRay = true;
			_stage = 0;
			_stageAt = _now;
			_stageWhy = "shieldRetry";
			return false;
		}
		++_st.teleShieldFrames;
		// integrator (in game hf 12:25, HF3: he shielded the caged essential FF0010D6 that never hit the player while the other caged
		// one, FF0010E7, shot the player from 0.43 to 0.30 - no defend jump either, "notHit"): the shield (and its defend jump) goes
		// to the attacker of the player that HURT it within tele.defendHitWindow s when the target did not - the most recent one
		const Cand* sc = &c;
		if (f.tele.shieldHurter && !(c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= f.tele.hitWindow + kTiny)) {
			double best = -1e9;
			for (std::size_t k = 0; k < f.cands.size() && k < _info.size(); ++k) {
				const Cand& o = f.cands[k];
				const Info& in = _info[k];
				if (o.id == c.id || !TeleCase(o, f) || !in.m || in.excluded || !o.hostile) continue;
				if (o.lastHitPlayerAt > -1e8 && _now - o.lastHitPlayerAt <= f.tele.hitWindow + kTiny && o.lastHitPlayerAt > best) {
					best = o.lastHitPlayerAt;
					sc = &o;
				}
			}
			if (sc != &c) ++_st.teleShieldHurter;
		}
		out.teleShieldId = sc->id;
		out.teleJumpOnly = true;
		if (std::string_view(_stageWhy) != "shield") _stageWhy = "shield";
		// the Pilot permitted the defend jump at it (the shield held tele.shieldFirst s and the player was hit again, or the
		// player is below tele.defendFreeHp; within the budget): DefendJump picks an unseen landing (or a seen one while the
		// permit allows it) and asks; the runtime judges both ends again before the move. A refused ask waits blinkGap s.
		if (f.tele.jumpId != 0 && f.tele.jumpId == sc->id && _now - _lastBlinkAt >= p.blinkGap - kTiny && _now - _lastBlinkTryAt >= 0.1 - kTiny &&
			_now - _djTryAt >= p.blinkGap - kTiny) {
			++_st.teleJumpAsks;
			if (DefendJump(*sc, f, p, w, out)) {
				out.target = sc->id;  // (the gate asks for this id; a jump to the hurter is not the target's: OnOwnBlink -> OnTeleport, a fresh pick there)
				return true;
			}
		}
		// hover at the shield spot (HOOKS s10 C15 / D): his torso ON the line from its chest to the player's chest, shieldDist u
		// from the player's chest (the shooter's line-of-fire test meets his capsule, its bullets his drawn body); re-solved every
		// frame as either moves (= core/pilot ShieldSpot)
		const Vec3   pChest = TelePlayerChest(f);  // (integrator: never a stale chest far from the player)
		const Vec3   d = sc->chest - pChest;
		const double L = Length(d);
		const double k = L > 1e-6 ? std::min(std::max(0.0, f.tele.shieldDist), std::max(0.0, L - 40.0)) / L : 0.0;
		const Vec3   spotChest = pChest + d * k;
		const Vec3   spotFeet = spotChest - (f.meChest - f.meFeet);
		const double vmax = f.indoors ? std::min(p.approachMax, p.indoorMaxSpeed) : p.approachMax;
		out.teleShieldSpot = spotFeet;
		// completion lane 3 (brain.teleShieldPress, Tele 1; soak 131156: 9 of the 59 FB stretches were this straight flight to the
		// spot pressed into a wall - the chain had carried him to the far side of it): pressed teleShieldPress s, the navmesh leg
		// to the player for 2 s (as TeleNearLeg), then the straight flight again
		if (p.teleNoPress >= 0.5 && p.teleShieldPress > 0 && p.nav >= 0.5 && w.NavAvailable()) {
			if (f.blockedFor >= p.teleShieldPress - kTiny && _now >= _teleShieldNavUntil) _teleShieldNavUntil = _now + 2.0;
			if (_now < _teleShieldNavUntil && Length(spotChest - f.meChest) > 120.0) {
				Cand goal;  // the player as the goal (the return's navmesh leg id)
				goal.id = kReturnNavId;
				goal.feet = f.playerFeet;
				goal.chest = pChest;
				goal.height = std::max(64.0, (pChest.z - f.playerFeet.z) / 0.68);
				goal.head = f.playerFeet + Vec3{ 0, 0, goal.height };
				goal.vel = f.playerVel;
				if (FollowNav(f, p, w, out, goal)) {
					out.vMax = std::min(out.vMax, vmax);
					out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
					++_st.teleShieldNav;
					_aiChasing = false;
					_wasFarLeg = false;
					return true;
				}
			}
		}
		out.move = Move::kApproach;
		out.vDes = ApproachVDes(f.meFeet, f.meChest, spotFeet, spotChest, vmax, p, f.meVel);
		out.aim = spotChest;
		out.aimVel = f.playerVel;
		out.accel = p.flyAccel;
		out.vMax = vmax;
		out.planKind = Plan::kDirect;
		out.directClear = false;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		_aiChasing = false;
		_wasFarLeg = false;
		return true;
	}

	// R6 s10 fix lane 3 (Tele 1, gate item 6b; GT20 17:41:08.269-11.314: the melee raider FF001CDD beating the player was picked
	// 1110 u away, hidden, and the reach chain went sky -> door -> doorNoProg -> shieldOther / the blocked-pinner shield with him
	// 1167 u from the spot, then the defend jump; the player died): an attacker of the player standing AT the player is reachable
	// wherever the player is - he flies to the player's side (toward it; straight when the line is clear, else the navmesh leg to
	// the player), where his eyes find it and the normal approach punches it. No shield, no jump from afar. Ends within
	// 0.6 x tele.nearPlayerR of the player (the chain / shield there).
	namespace
	{
		// the near-player leg's spot: the player's side toward the foe - on the player -> foe line, at most 120 u from the player
		// (s13 fix round 3 lane 4: shared by TeleNearLeg and the stall leg's arrival)
		Vec3 NearSpot(const Cand& c, const Facts& f)
		{
			const Vec3 pChest = TelePlayerChest(f);
			const Vec3 dir = Normalize(c.chest - pChest, FlatDir(f.meFeet - f.playerFeet));
			return pChest + dir * std::min(120.0, 0.5 * Length(c.chest - pChest));
		}
	}
	bool Brain::TeleNearLeg(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out, double a_nearR, bool a_stall)
	{
		// (s10f integrator: brain.nearPlayerLeg gives the same leg with Tele 0 - the old reach chain's sky / door legs stalled
		// 1300 u from a melee raider beating the player, soak dt3 21:10:00-04)
		// (s13 fix round 3 lane 4: a_nearR > 0 = the brain.nearStallR leg's radius, any Tele value)
		const double nearR = a_nearR > 0 ? a_nearR : (TeleOn(f) ? f.tele.nearPlayerR : p.nearPlayerLeg);
		if (!(nearR > 0)) return false;
		if (!(c.attackingPlayer && !c.killPending && c.loaded && c.sameSpace)) return false;
		const Vec3   pChest = TelePlayerChest(f);
		const double dFoe = Length(c.chest - pChest);
		if (!(dFoe <= nearR)) return false;
		// at the player: the chain / shield take over (s13 fix round 3 lane 4: the stall leg ends at the SPOT instead - NearStall -,
		// near the player on the far side of what held him is not there yet)
		if (!a_stall && Length(f.meChest - pChest) <= 0.6 * nearR) return false;
		// the player's side toward it: on the player -> foe line, at most 120 u from the player
		const Vec3   spotChest = NearSpot(c, f);
		const Vec3   spotFeet = spotChest - (f.meChest - f.meFeet);
		if (_now - _nearRayAt >= 0.1 - kTiny && CanCast(1)) {
			_nearRayAt = _now;
			_nearClear = RClear(w, f.meChest, spotChest);
		}
		// a fresh leg (not driven in the last 0.3 s): its own progress clock. The straight flight on a clear RAY that makes no
		// progress (the mover holds his capsule on an edge the thin ray passes, F3-6B: an awning lip) hands over to the navmesh
		// leg for 2 s
		const double dSpot = Length(spotChest - f.meChest);
		if (_now - _nearAt > 0.3 || _now < _nearAt) {
			_nearBest = dSpot;
			_nearProgAt = _now;
			// (s13 fix round 3 lane 4: the stall leg starts on the navmesh leg - his straight approach to the foe is what stalled)
			_nearNavUntil = a_stall ? _now + 2.0 : -1e9;
		} else if (dSpot < _nearBest - 20.0) {
			_nearBest = dSpot;
			_nearProgAt = _now;
		}
		if (_nearClear && _now >= _nearNavUntil && (f.blockedFor >= 0.5 - kTiny || _now - _nearProgAt >= 0.6 - kTiny)) {
			_nearNavUntil = _now + 2.0;
			_nearProgAt = _now;
		}
		const double vmax = f.indoors ? std::min(p.approachMax, p.indoorMaxSpeed) : p.approachMax;
		bool         flying = false;
		const auto   straight = [&]() {
			out.move = Move::kApproach;
			out.vDes = ApproachVDes(f.meFeet, f.meChest, spotFeet, spotChest, vmax, p, f.meVel);
			out.aim = spotChest;
			out.aimVel = f.playerVel;
			out.accel = p.flyAccel;
			out.vMax = vmax;
			out.planKind = Plan::kDirect;
			return true;
		};
		if (_nearClear && _now >= _nearNavUntil) {
			flying = straight();
		} else if (p.nav >= 0.5 && w.NavAvailable()) {
			Cand goal;  // the player as the goal (the return's navmesh leg id)
			goal.id = kReturnNavId;
			goal.feet = f.playerFeet;
			goal.chest = pChest;
			goal.height = std::max(64.0, (pChest.z - f.playerFeet.z) / 0.68);
			goal.head = f.playerFeet + Vec3{ 0, 0, goal.height };
			goal.vel = f.playerVel;
			flying = FollowNav(f, p, w, out, goal);
			if (flying) out.vMax = std::min(out.vMax, vmax);
		}
		// s13 fix round 3 lane 4: the stall leg whose navmesh leg has no way flies straight when that line is clear
		if (!flying && a_stall && _nearClear) flying = straight();
		if (!flying) return false;
		_nearAt = _now;
		const Info* in = nullptr;
		for (std::size_t k = 0; k < f.cands.size() && k < _info.size(); ++k)
			if (f.cands[k].id == c.id) in = &_info[k];
		out.target = c.id;
		out.tier = in ? in->tier : _tTier;
		out.cls = c.cls;
		out.visible = false;
		out.pin = in && in->pin;
		out.dist = Length(c.chest - f.meChest);
		out.face = FlatDir(c.feet - f.meFeet);
		out.mayAttack = false;
		out.directClear = false;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		out.hunt = HuntState::kLocal;
		out.stageWhy = a_stall ? "nearStall" : "nearPlayer";
		_aiChasing = false;
		_wasFarLeg = false;
		_teleShieldId = 0;  // (not a shield: no shield clock for the Pilot)
		++(a_stall ? _st.nearStallFrames : _st.teleNearFrames);
		return true;
	}

	// R6 step 13 fix round 3 lane 4 (brain.nearStallR; s13r2 gate GK2 dt2 20:32:08.6-12.5, run 20261024-202435): the melee raider
	// FF001667 was his target from 08.63 (701 u, seen); it walked to the player and hit it at 09.57 while he got no closer (_best
	// 701 u, noProg 0.27 -> 1.28 s), the sky stage took him from z 600 to 1371 at 09.71 (hidden), he flew over and hung ~900 u right
	// above it (10.94 vis 1 move H noProg 2.30, 11.19 meV 85, 12.13 blockedFor 0.46) until the stuck law's navmesh leg took him down
	// in 0.6 s at 11.9 - FF001666, the next raider at the player, took it 0.89 -> 0.17 at 12.497. A foe attacking the player AT the
	// player is reachable wherever the player is: when he gets no closer to it for nearStallT s he flies to the player's side (the
	// navmesh leg to the player first - the player always stands on walkable ground), where the normal approach punches it.
	void Brain::NearStallEnd(std::uint64_t& a_why, bool a_block)
	{
		++a_why;
		if (a_block) {
			_nsBlockId = _nsId;
			_nsBlockUntil = _now + 2.0;
		}
		_nsId = 0;
		_nsUntil = -1e9;
	}

	bool Brain::NearStall(const Cand& c, const Facts& f, const Params& p)
	{
		if (!(p.nearStallR > 0)) {
			_nsId = 0;
			return false;
		}
		if (_nsId != 0 && _nsId != c.id) {  // (a new pick: ResetTargetState already cleared it; any other way: dropped)
			_nsId = 0;
			_nsUntil = -1e9;
		}
		const bool latched = _nsId == c.id;
		if (latched && _now >= _nsUntil) {  // ran out: the old chain, no new stall leg on this foe for 2 s
			NearStallEnd(_st.nearStallTimeout, true);
			return false;
		}
		const double R = p.nearStallR, Rk = R + 100.0, atPl = 0.6 * Rk;
		if (!(c.attackingPlayer && !c.killPending && c.loaded && c.sameSpace)) {
			if (latched) NearStallEnd(_st.nearStallDrop, false);
			return false;
		}
		const Vec3   pChest = TelePlayerChest(f);
		const double dFoe = Length(c.chest - pChest), dMe = Length(f.meChest - pChest);
		if (latched) {
			// at the player's side: at the leg's spot (the player -> foe line, <= 120 u from the player) or within punch range of the
			// foe - near the player on the far side of what held him (a fence, a lip) is NOT there yet: the leg goes on round it
			if (Length(NearSpot(c, f) - f.meChest) <= 100.0 || Length(c.chest - f.meChest) <= 150.0) {
				// the normal approach from here - the reach chain on this foe starts over from this spot (its sky / navmesh stage was
				// planned from where the stall began) and its progress clocks with it
				NearStallEnd(_st.nearStallArrive, false);
				_stage = -1;
				_stageAt = _now;
				_stageWhy = "";
				_stillT = _hiddenT = _hiddenTotal = 0;
				_triedNav = _triedRay = _doorAi = _aiTried = false;
				_planFor = 0;
				_plan = Plan{};
				_wpIdx = 0;
				_planAt = -1e9;
				_noPathSince = _exhaustedSince = -1;
				_directBlocked = 0;
				_directAt = -1e9;
				_chainAt = _directClearAt = _directBlockAt = -1e9;
				_stuckChainUntil = -1e9;
				_stuckSince = -1;
				_best = _farBest = 1e18;
				_progAt = _farProgAt = _now;
				return false;
			}
			if (dFoe > Rk) {
				NearStallEnd(_st.nearStallDrop, false);  // it left the player
				return false;
			}
			// explore m1 fix lane 3 (brain.closeDefendR; xp4 08:52:26.65-27.58: the leg started before FF00172C's first hit at 27.19 and
			// flew on, held, 0.4 s after it): it hurts the player now, indoors, right at it, and he cannot touch it - the defend blink
			if (CloseDefendCase(c, f, p)) {
				++_st.m1CloseStall;
				NearStallEnd(_st.nearStallDrop, false);
				return false;
			}
			return true;
		}
		if (c.id == _nsBlockId && _now < _nsBlockUntil) return false;
		// (not when he is in punch range of it already - 150 u, the arrival radius below; 250 u above it on a roof he is not)
		if (dFoe > R || dMe <= atPl || Length(c.chest - f.meChest) <= 150.0) return false;
		// explore m1 fix lane 3 (brain.closeDefendR): indoors a foe hurting the player right at it that he cannot touch gets the
		// defend blink (ChooseMove), not a leg round to the player's side first (xp4 Mass Fusion lobby: 0.9 s held at the balcony)
		if (CloseDefendCase(c, f, p)) {
			++_st.m1CloseStall;
			return false;
		}
		if (_now - _progAt < std::max(0.0, p.nearStallT) - kTiny) return false;
		_nsId = c.id;
		_nsAt = _now;
		_nsUntil = _now + std::max(0.5, p.nearStallFor);
		++_st.nearStalls;
		return true;
	}

	bool Brain::NearStallLeg(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out)
	{
		// the leg itself held at geometry (FB) 0.75 s: the old chain gets its turn (no new stall leg on this foe for 2 s)
		if (_nsId == c.id && f.blockedFor >= 0.75 - kTiny && _now - _nsAt >= 0.75 - kTiny) {
			NearStallEnd(_st.nearStallHeld, true);
			return false;
		}
		_navNoPull = true;  // (the navmesh corners, never a string pull through what held him)
		const bool flew = TeleNearLeg(c, f, p, w, out, p.nearStallR + 100.0, true);
		_navNoPull = false;
		if (flew) return true;
		// no way to the player's side (no navmesh way, the straight line blocked) or already there: the old movement this frame;
		// a leg that never flew is no stall leg (it starts again after the 2 s block if the stall goes on)
		if (_nsId == c.id) NearStallEnd(_st.nearStallNoWay, true);
		return false;
	}

	// explore m1 fix lane 3 (brain.closeDefendR; xp4 Mass Fusion lobby, run 20261025-084739-xp4-resume33-s13c 08:52:26.06-28.00: two
	// gunners on the balcony 124-191 u from the player; he was 145-191 u from FF00172C - 'CONTACT|around none' at 26.06 / 26.53, the chain
	// 'nav' at 26.51 (the navmesh: 'nofrom'), the stall leg to the player's side at 26.65 held at the balcony until ~27.5, the chain again
	// at 27.58, push 27.76, the D2 defend blink at 27.96 = 0.77 s after FF00172C's first hit (27.19); then FF00158B the same, 1.4 s from
	// its first hit; the player 1.00 -> 0.14 of a cut max health in 1.3 s): indoors (the D2 defend blink's ground), a killable foe that hit
	// the player within d2DbHitWin s, within closeDefendR of the player and within closeDefendMeR of him (at any distance below: pressed
	// at the balcony 110 u from it he is not at it - the BLINK stage's own near rule is lifted for this stage, cdNow), that he cannot touch
	// - his eyes lost it closeDefendT s ago, his punch found no spot round it (around none) or every contact line has been blocked
	// closeDefendT s - is the defend blink's case NOW. Pure (reads the brain's sight memory)
	// FINISH lane A_combat_zone (brain.czDefendT; the x1 gate soak GK1 cz2, run 20261025-144443-r6s1444-c-soak10-120 14:52:47.6-56.1: the
	// caged arena gunner FF001630 his only target for 8.5 s - from 49.1 on 430-490 u from it, sliding along the cage (FB latched in 0.02-0.83
	// s bursts, meV 92-550, stuckT / noProg ~0), its sight flickering through the bars, the navmesh plan restarted every 0.4-0.5 s (move
	// A <-> N, 'NAV|stage -1->0 why=nav', a 540-740 u way never flown to its end) - and the D2 defend blink only at 53.159, 2.04 s after
	// its first hit (51.116): 4 hits, 0.78 -> 0.24, god mode back; the s13 gate soak K3 cz2 0.29 and the hf cage reach 0.50 were the same
	// "old reach chain held at bars / doors" - tests/s10_cz.inc CZ-CAGE): indoors (the D2 defend blink's ground), a killable foe that hit
	// the player within d2DbHitWin s, within closeDefendMeR u of him (and not already within punch reach), that he has got no closer to
	// by czDefendStep u for czDefendT s - whatever his eyes or the chain said meanwhile - while pressed at something (body contact / the FB
	// held clock) within that time, is the defend blink's case NOW. Pure (reads the no-progress clock ChooseMove keeps)
	// FINISH A2 fix lane 4 (brain.czThrowR; the finA gate's HF3 0.22, hf 1080u 20261026-091752: the caged molotov raider FF0010EA aimed at
	// the player from 09:20:50.8 on - his target 51.35-52.23 'blinkWait' -> 'blinkCap', 54.51-57.22 and 59.32-00.91 'd2Wait' -> 'd2NoBlink':
	// every landing / his take-off in the player's view, and the D2 defend blink that may land in view waits for a HIT - its first molotov
	// hit 01.278, his blink 01.279, the kill 03.472 after 3 hits; the user's rule: a thrower holding one ready at the player is his first
	// target): indoors (the D2 defend blink's ground), a killable thrower (Cand::explosive) whose combat target is the player, within czThrowR
	// u of the player, counts as hurting the player NOW for the D2 defend blink - in the reach chain's BLINK stage ONLY (its hurtNow), i.e.
	// after his run AND push at it got nowhere (round 2, after the verifier: not the push stage's d2CageSkip hand-over nor the cage defend's
	// hit window - those would cut a TABS run short after 0.25 / 0.5 s). Essentials stay with their own rules (never killed, the knock-down
	// cap). Pure
	bool Brain::CzThrowCase(const Cand& c, const Facts& f, const Params& p) const
	{
		if (!(p.czThrowR > 0) || !f.interiorCell || f.ext.on || TeleOn(f) || p.d2DefendBlink < 0.5) return false;
		if (!(c.explosive && c.combatWithPlayer && c.attackingPlayer && c.hostile && !c.killPending && c.loaded && c.sameSpace && !c.essentialOnly)) return false;
		return Length(c.chest - TelePlayerChest(f)) <= p.czThrowR + kTiny;
	}

	// B2 fix lane 1 (brain.atPlFarR; the finB gate's soak god-back GK1 20261027-011106, the Concord Speakeasy wave 01:19:00.4-05.9 - see
	// brain.h): a killable foe whose combat target is the player, standing AT the player, is hurting it NOW for the D2 defend blink - its
	// blow is coming; the D2 rules waited for its first HIT (0.57 -> 0.23 in one hit there). Indoors only (the D2 defend blink's ground)
	bool Brain::AtPlayerCase(const Cand& c, const Facts& f, const Params& p) const
	{
		if (!(p.atPlFarR > 0) || !f.interiorCell || f.ext.on || TeleOn(f) || p.d2DefendBlink < 0.5 || p.navBlink < 0.5) return false;
		if (!(c.attackingPlayer && c.hostile && !c.killPending && c.loaded && c.sameSpace && !c.essentialOnly)) return false;
		return Length(c.chest - TelePlayerChest(f)) <= p.atPlFarR + kTiny;
	}
	// ... and the reach chain hands its NAV / door leg over to the BLINK stage when that foe is the long way round: he is on another floor
	// (atPlFarDz) or the navmesh way is atPlFarK x the straight line (GK1: 650-800 u straight, 900-1200 u by the stairs door, 271-323 u
	// below him), atPlFarT s into the chain (a way that is short gets him there before; a foe within navBlinkNear is the punch's)
	bool Brain::AtPlayerFar(const Cand& c, const Facts& f, const Params& p) const
	{
		if (!AtPlayerCase(c, f, p)) return false;
		const double straight = Length(c.chest - f.meChest);
		if (!(straight > p.navBlinkNear + kTiny)) return false;
		const bool otherFloor = p.atPlFarDz > 0 && std::abs(c.feet.z - f.meFeet.z) >= p.atPlFarDz - kTiny;
		const bool longWay = p.atPlFarK > 0 && _nav.forId == c.id && _nav.ok && _nav.remain >= p.atPlFarK * straight - kTiny;
		return otherFloor || longWay;
	}
	namespace
	{
		constexpr double kAtPlRetry = 3.0;  // s: one hand-over per foe (a BLINK stage with no landing: the long way runs next)
	}
	bool Brain::AtPlayerFarHand(const Cand& c, const Facts& f, const Params& p) const
	{
		if (_now - _chainAt < p.atPlFarT - kTiny) return false;
		// doorOpenAt also records unsuccessful queries. Only an actual opening/smash creates this swing window.
		// An attacker already at the player may use its defend shortcut once the leaf has had time to swing.
		if (_nav.forId == c.id && _now < _nav.doorWaitUntil) return false;
		const auto m = _mem.find(c.id);
		if (m != _mem.end() && _now >= m->second.atPlHandAt - kTiny && _now - m->second.atPlHandAt < kAtPlRetry - kTiny) return false;
		return AtPlayerFar(c, f, p);
	}

	bool Brain::CzDefendCase(const Cand& c, const Facts& f, const Params& p, double a_d) const
	{
		if (!(p.czDefendT > 0) || !f.interiorCell || f.ext.on || TeleOn(f) || p.d2DefendBlink < 0.5 || p.navBlink < 0.5) return false;
		if (!(c.attackingPlayer && !c.killPending && c.loaded && c.sameSpace && !c.essentialOnly)) return false;
		if (!(c.lastHitPlayerAt > -1e8 && _now >= c.lastHitPlayerAt - kTiny && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny)) return false;
		if (a_d > p.closeDefendMeR + kTiny || a_d <= p.approachMin + 50.0) return false;
		// ... and he was pressed at something (his body contact / the mover's held clock) within the last czDefendT s: a free flight that
		// keeps its distance (a chase, a dash round it) is no stall
		return _czId == c.id && _now - _czAt >= p.czDefendT - kTiny && _now >= _czPressAt - kTiny && _now - _czPressAt <= std::max(0.25, p.czDefendT) + kTiny;
	}

	bool Brain::CloseDefendCase(const Cand& c, const Facts& f, const Params& p)
	{
		if (!(p.closeDefendR > 0) || !f.interiorCell || f.ext.on || TeleOn(f) || p.d2DefendBlink < 0.5 || p.navBlink < 0.5) return false;
		if (!(c.attackingPlayer && !c.killPending && c.loaded && c.sameSpace && !c.essentialOnly)) return false;
		if (!(c.lastHitPlayerAt > -1e8 && _now >= c.lastHitPlayerAt - kTiny && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny)) return false;
		if (Length(c.chest - TelePlayerChest(f)) > p.closeDefendR + kTiny) return false;
		const double d = Length(c.chest - f.meChest);
		if (d > p.closeDefendMeR + kTiny) return false;  // (any distance below: pressed at the balcony 110 u from it he is not at it)
		const auto   it = _mem.find(c.id);
		const bool   hid = it != _mem.end() && it->second.vis == 0 && it->second.occSince >= 0 && _now - it->second.occSince >= p.closeDefendT - kTiny;
		const bool   around = f.aroundNoneId != 0 && f.aroundNoneId == c.id && f.aroundNoneAge >= 0 && f.aroundNoneAge <= 0.25;
		const bool   blk = f.contactBlockedFor >= std::max(0.05, p.closeDefendT) - kTiny;
		return hid || around || blk;
	}

	// completion lane 3 (brain.teleNoPress; no teleports, Tele 1 only). Tele-1 soak 20261021-131156 in1 (ArcJet): the robots
	// 000DED97 / 99 / 9A / 9B behind a shut way, none hurting the player: for each the navmesh leg pressed 2-4 s at the same spot,
	// the push, then the BLINK stage's refused blink became 'd2Wait' (2 s more push into the wall), 'd2NoBlink', give-up 2.5 s,
	// and the next robot behind the same way - 21 FB stretches >= 1 s in that cell; the Tele-0 path blinked in. A chase with no
	// physical way now ends without pressing, and the way is remembered as shut (for its neighbours too) until teleShutHold s.
	bool Brain::TeleNoPressCase(const Cand& c, const Facts& f, const Params& p) const
	{
		if (!TeleOn(f) || p.teleNoPress < 0.5) return false;
		if (TeleCase(c, f)) return false;  // it hurts the player: the shield (step 10), never skipped
		// an attacker standing at the player: the flight to the player's side (TeleNearLeg) owns it
		const double nearR = std::max(400.0, f.tele.nearPlayerR);
		if (c.attackingPlayer && Length(c.chest - TelePlayerChest(f)) <= nearR) return false;
		return true;
	}

	bool Brain::TeleShutNow(const Cand& c, const Facts& f, const Params& p) const
	{
		if (!(p.teleShutHold > 0)) return false;
		const auto it = _mem.find(c.id);
		if (it != _mem.end() && it->second.teleShutUntil > _now && it->second.teleShutUntil - _now <= p.teleShutHold + kTiny) return true;
		for (const TeleShutWay& s : _teleShut) {
			if (!(s.until > _now) || s.until - _now > p.teleShutHold + kTiny) continue;
			if (Length(c.feet - s.foe) <= p.teleShutR && Length(f.meFeet - s.me) <= p.teleShutFrom) return true;
		}
		return false;
	}

	void Brain::TeleShutMark(const Cand& c, const Facts& f, const Params& p)
	{
		if (!(p.teleShutHold > 0)) return;
		M(c.id).teleShutUntil = _now + p.teleShutHold;
		TeleShutWay& s = _teleShut[_teleShutTop];
		_teleShutTop = (_teleShutTop + 1) % kTeleShut;
		s.foe = c.feet;
		s.me = f.meFeet;
		s.until = _now + p.teleShutHold;
		++_st.teleShutMarks;
	}

	void Brain::TeleShutClear()
	{
		for (TeleShutWay& s : _teleShut) s = TeleShutWay{};
		_teleShutTop = 0;
		_teleShieldNavUntil = -1e9;
		_guardRayAt = _guardAt = _guardProgAt = _guardNavUntil = -1e9;
		_guardClear = false;
		for (auto& kv : _mem) kv.second.teleShutUntil = -1e9;
	}

	// completion lane 3: the chain on a foe he has no way to ended (teleShut / teleNoWay) but the foe is still his target (the
	// no-idle fallback keeps an attacker of the player: never tgt=0 while it attacks the player, P09). Instead of standing still
	// at the wall he holds the player's side toward it, tele.shieldDist (150 u) from the player's chest - he is back with the
	// player, between it and the foe, and picks the foe up the moment it shows (sight) or hurts the player (the shield case).
	bool Brain::TeleGuardLeg(const Cand& c, const Facts& f, const Params& p, IWorld& w, Out& out)
	{
		const Vec3   pChest = TelePlayerChest(f);
		const Vec3   dir = Normalize(c.chest - pChest, FlatDir(f.meFeet - f.playerFeet));
		const double dFoe = Length(c.chest - pChest);
		const double k = std::min(std::max(60.0, f.tele.shieldDist > 0 ? f.tele.shieldDist : 150.0), std::max(60.0, 0.5 * dFoe));
		const Vec3   spotChest = pChest + dir * k;
		const Vec3   spotFeet = spotChest - (f.meChest - f.meFeet);
		const double vmax = f.indoors ? std::min(p.approachMax, p.indoorMaxSpeed) : p.approachMax;
		if (_now - _guardRayAt >= 0.1 - kTiny && CanCast(1)) {
			_guardRayAt = _now;
			_guardClear = RClear(w, f.meChest, spotChest);
		}
		const double dSpot = Length(spotChest - f.meChest);
		if (_now - _guardAt > 0.3 || _now < _guardAt) {  // a fresh leg: its own progress clock
			_guardBest = dSpot;
			_guardProgAt = _now;
			_guardNavUntil = -1e9;
		} else if (dSpot < _guardBest - 20.0) {
			_guardBest = dSpot;
			_guardProgAt = _now;
		}
		// the straight flight held at something or making no progress (and not there yet): the navmesh leg to the player for 2 s
		if (dSpot > 100.0 && _now >= _guardNavUntil && (f.blockedFor >= 0.5 - kTiny || _now - _guardProgAt >= 0.6 - kTiny)) {
			_guardNavUntil = _now + 2.0;
			_guardProgAt = _now;
		}
		bool flying = false;
		if (_guardClear && (_now >= _guardNavUntil || dSpot <= 100.0)) {
			out.move = Move::kApproach;
			out.vDes = ApproachVDes(f.meFeet, f.meChest, spotFeet, spotChest, vmax, p, f.meVel);
			out.aim = spotChest;
			out.aimVel = f.playerVel;
			out.accel = p.flyAccel;
			out.vMax = vmax;
			out.planKind = Plan::kDirect;
			flying = true;
		} else if (p.nav >= 0.5 && w.NavAvailable()) {
			Cand goal;  // the player as the goal (the return's navmesh leg id)
			goal.id = kReturnNavId;
			goal.feet = f.playerFeet;
			goal.chest = pChest;
			goal.height = std::max(64.0, (pChest.z - f.playerFeet.z) / 0.68);
			goal.head = f.playerFeet + Vec3{ 0, 0, goal.height };
			goal.vel = f.playerVel;
			flying = FollowNav(f, p, w, out, goal);
			if (flying) out.vMax = std::min(out.vMax, vmax);
		}
		if (!flying) return false;
		_guardAt = _now;
		out.face = FlatDir(c.feet - f.meFeet);
		out.directClear = false;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		out.stageWhy = "teleGuard";
		_aiChasing = false;
		_wasFarLeg = false;
		++_st.teleGuardFrames;
		return true;
	}

	// ------------------------------------------------------------------------------------------- ROUND5: push, doors, interiors
	bool Brain::InteriorHuntOk(const Cand& c, const Ctx& cx) const { return cx.interiorHunt && c.sameSpace && c.loaded && c.hostile; }

	// ROUND5 (in game round 4 R2: museum "0->2 why=doorDead" then a blink in and a put-back out through the Expert-locked
	// door 000509AD): a locked door on the segment is forced open (a lock a player could pick, not a load door) and he hovers
	// navDoorOpenWait s while it swings. brain.navSmashLocked 0: never.
	bool Brain::SmashOnSegment(const Facts&, const Params& p, IWorld& w, const Vec3& a_from, const Vec3& a_to)
	{
		if (p.navSmashLocked < 0.5) return false;
		const int n = w.SmashDoors(a_from, a_to, p.navDoorOpenR);
		if (n <= 0) return false;
		_st.doorsSmashed += static_cast<std::uint64_t>(n);
		return true;
	}

	// ROUND5 (in game round 4 R2: every engine-AI stage left him standing - museum 0 / 2 / 40 / 53 u in 0.9-1.8 s, MQ102
	// bloatfly 2 u in 1.5 s, b60 run 0 u in 1.1-3.7 s - so the chain stood still, then teleported): he flies himself. The
	// goal is the next corner of this target's navmesh leg (its waypoints are kept when the leg died at a blocked spot), else
	// the goal itself; at pushV with an arrival brake. Pressed against something for pushSideAfter s: a sidestep at 90
	// degrees to the goal line (the free side first, one ray each), with a forward try every pushForward s; no progress
	// toward the goal for pushSideSwap s on that side: the other side. The capsule is Havok-swept: never through a wall.
	void Brain::FlyPush(const Facts& f, const Params& p, IWorld& w, Out& out, const Vec3& a_goalChest, std::uint32_t a_navId)
	{
		if (_pushFor != a_navId) {
			_pushFor = a_navId;
			_pushSide = 0;
			_pushSwaps = 0;
			_pushHead = {};
			_pushHeadSwaps = -1;
			_pushDrop = 0;
			_pushSideAt = _pushFreeAt = _pushFwdUntil = -1e9;
			_pushBest = 1e18;
			_pushProgAt = _now;
			_pushRealProgAt = _now;  // ROUND5 A2: the real progress clock of this push
			_pushRealBest = 1e18;
			_pushAimIdx = -1;
		}
		Vec3 aim = a_goalChest;
		int  aimIdx = -1;
		// (ROUND5 A2: a leg that ended short of an off-navmesh foe has nothing left to give: straight at the foe)
		const bool offMesh = _nav.forId == a_navId && _nav.offMesh;
		if (_nav.forId == a_navId && !_nav.wp.empty() && !offMesh) {
			int i = std::clamp(_nav.idx, 0, static_cast<int>(_nav.wp.size()) - 1);
			while (i + 1 < static_cast<int>(_nav.wp.size()) && Length(_nav.wp[i] - f.meChest) <= p.navReach) ++i;
			_nav.idx = std::max(_nav.idx, i);
			// the last corner reached: the goal itself
			if (!(i == static_cast<int>(_nav.wp.size()) - 1 && Length(_nav.wp[i] - f.meChest) <= p.navReach)) {
				aim = _nav.wp[i];
				aimIdx = i;
			} else {
				aimIdx = static_cast<int>(_nav.wp.size());
			}
		}
		const double dGoal = Length(a_goalChest - f.meChest);
		if (dGoal < _pushBest - p.progressStep) {
			_pushBest = dGoal;
			_pushProgAt = _now;
		}
		// ROUND5 A2 (gate A1, c120-r5: the push slid back and forth for 6 s, noProg 10.5 s, 728 of 1270 frames pressed): real
		// progress = closer to the goal by progressStep, or the next navmesh corner reached. Side swaps do not count; the
		// chain's push caps (navPushNoProg / navOffPush) read this clock.
		if (dGoal < _pushRealBest - p.progressStep) {
			_pushRealBest = dGoal;
			_pushRealProgAt = _now;
		}
		if (aimIdx > _pushAimIdx) {
			if (_pushAimIdx >= 0) _pushRealProgAt = _now;
			_pushAimIdx = aimIdx;
		}
		Vec3         e = aim - f.meChest;
		const double dist = Length(e);
		Vec3         dir = dist > 1.0 ? e / dist : FlatDir(a_goalChest - f.meChest);
		const bool   pressed = f.blockedFor >= p.pushSideAfter - kTiny ||
		                     (f.bodyContact && f.intentSpeed > p.stuckIntent && f.actualSpeed < p.stuckRatio * f.intentSpeed);
		// the two sidestep headings: square to the goal line; with the goal (nearly) straight above / below him (a roof over
		// the player) a horizontal heading of his own: away from the player, turned 90 degrees on every swap
		auto sides = [&](Vec3& a_left, Vec3& a_right) {
			Vec3 fl = FlatDir(dir);
			if (Length(fl) < 0.5 || std::abs(dir.z) > 0.8) {  // the goal mostly below / above him
				if (_pushHeadSwaps != _pushSwaps || Length(_pushHead) < 0.5) {  // latched: it turns only on a swap
					Vec3 away = FlatDir(f.meChest - f.playerChest);
					if (Length(away) < 0.5) away = { 1, 0, 0 };
					const double a = std::atan2(away.y, away.x) + 1.5707963267948966 * static_cast<double>(_pushSwaps);
					_pushHead = { std::cos(a), std::sin(a), 0 };
					_pushHeadSwaps = _pushSwaps;
				}
				a_left = _pushHead;
				a_right = _pushHead * -1.0;
				return;
			}
			a_left = { -fl.y, fl.x, 0 };
			a_right = { fl.y, -fl.x, 0 };
		};
		if (pressed) {
			_pushFreeAt = -1e9;
			_pushFwdUntil = -1e9;  // a forward try that ran into it again is over
			Vec3 left{}, right{};
			sides(left, right);
			if (_pushSide == 0) {
				bool lc = true, rc = true;
				if (CanCast(2)) {
					lc = RClear(w, f.meChest, f.meChest + left * p.pushRay);
					rc = RClear(w, f.meChest, f.meChest + right * p.pushRay);
				}
				// both free / both shut: the side nearer the goal's lateral offset (the corner is that way more often)
				const double lat = Dot(Flat(a_goalChest - f.meChest), left);
				_pushSide = (lc && !rc) ? 1 : (rc && !lc) ? -1 : (lat >= 0 ? 1 : -1);
				_pushSideAt = _now;
				_pushSidePos = f.meChest;
				_pushProgAt = std::max(_pushProgAt, _now);
				++_st.pushSides;
			} else {
				// each swap gives the new side twice the time (1, 2, 4 s ...): a far corner is still found, no shuttle
				const double hold = p.pushSideSwap * std::pow(2.0, std::min(_pushSwaps, 4));
				// the sidestep itself is shut (a corner): the other side at once, it does not count as a tried side
				// (pressed on through the sidestep: the side runs into an angled wall / a corner)
				const bool sideShut = _now - _pushSideAt >= 1.4 * p.pushSideAfter - kTiny &&
				                      (f.blockedFor >= 1.4 * p.pushSideAfter - kTiny || Length(Flat(f.meChest - _pushSidePos)) < 40.0);
				if (sideShut || (_now - _pushSideAt >= hold - kTiny && _now - _pushProgAt >= p.pushSideSwap - kTiny)) {
					_pushSide = -_pushSide;
					_pushSideAt = _now;
					_pushSidePos = f.meChest;
					_pushProgAt = _now;
					if (!sideShut) ++_pushSwaps;
					++_st.pushSides;
				}
			}
		} else if (_pushSide != 0) {
			if (_pushFreeAt < -1e8) _pushFreeAt = _now;
			// free again: the forward line is tried after pushForward s of free sidestepping
			if (_now - _pushFreeAt >= p.pushForward - kTiny) {
				_pushFwdUntil = _now + p.pushForward;
				_pushFreeAt = -1e9;
			}
		}
		// FINISH B2 Fable (brain.retSideMax; finA2 soak GK4 21:36:18.6-21:36:25: the way back's sidestep at pushV ran 300-2300 u - from the
		// mesh end below the mezzanine up to the ArcJet upper floor, pressed there 2.7 s - before its 1 / 2 / 4 s hold ended): indoors, the
		// way back's sidestep turns back after retSideMax u of lateral travel; the side counts as tried (both tried: the wait, ReturnMove)
		if (p.retSideMax > 0 && a_navId == kReturnNavId && f.interiorCell && _pushSide != 0 && Length(Flat(f.meChest - _pushSidePos)) > p.retSideMax + kTiny) {
			_pushSide = -_pushSide;
			_pushSideAt = _now;
			_pushSidePos = f.meChest;
			_pushProgAt = _now;
			_pushFwdUntil = -1e9;
			++_pushSwaps;
			++_st.retSideCaps;
		}
		if (_pushSide != 0 && _now >= _pushFwdUntil) {
			Vec3 left{}, right{};
			sides(left, right);
			const Vec3 side = _pushSide > 0 ? left : right;
			// 15 degrees off the obstacle (105 from the goal line): he slides along it instead of grinding its face
			dir = Normalize(side * 0.966 - dir * 0.259, side);
		}
		// The goal far below / above (a roof over the player, a balcony floor over the foe; Concord: on the museum roof 1300 u
		// over the player): pressing at it never ends. Edge mode: along the latched heading until a ray straight down (up) to
		// the goal's level is clear, then straight down (up) to it, then the normal push. Blocked along the heading: it turns
		// 90 degrees. pushDropMin 0 = off.
		const double vgap = aim.z - f.meChest.z;
		// ROUND5 A2: an off-navmesh foe (a balcony, a ledge) is often only ~100 u above / below: edge mode from 0.6 navOffGoal
		const double dropMin = offMesh && p.navOffGoal > 0 ? std::min(p.pushDropMin, 0.6 * p.navOffGoal) : p.pushDropMin;
		if (p.pushDropMin > 0 && std::abs(vgap) > dropMin && (pressed || _pushDrop != 0)) {
			const double sgn = vgap < 0 ? -1.0 : 1.0;
			if (_pushDrop == 0) {
				_pushDrop = 1;
				_pushDropAt = -1e9;
				++_st.pushDrops;
			}
			if (_pushDrop == 1 && pressed && _now - _pushSideAt >= p.pushSideSwap - kTiny) {  // the heading itself is blocked: turn
				++_pushSwaps;
				_pushSideAt = _now;
			}
			if (_pushDrop == 2 && pressed) _pushDrop = 1;  // the drop hit something: on along the heading
			if (_pushDrop == 1 && _now - _pushDropAt >= p.navPullEvery - kTiny && CanCast(1)) {
				_pushDropAt = _now;
				const Vec3 to{ f.meChest.x, f.meChest.y, aim.z - sgn * 20.0 };
				if (RClear(w, f.meChest, to)) _pushDrop = 2;
			}
			if (_pushDrop == 2) {
				dir = { 0, 0, sgn };
			} else {
				Vec3 left{}, right{};
				dir = { 0, 0, sgn };  // the latched horizontal heading (the "goal straight below / above" branch of sides())
				sides(left, right);
				dir = Normalize(left + Vec3{ 0, 0, -0.1 * sgn }, left);  // a touch away from the roof / floor he is on
			}
		} else if (_pushDrop != 0) {
			_pushDrop = 0;  // at the goal's level: the normal push
		}
		const bool   tight = f.interiorCell || f.lowCeiling;
		const double vcap = std::min(p.pushV, tight ? p.navVMaxIndoor : p.navVMax);
		const double sp = std::min(vcap, std::max(p.navVMin, std::sqrt(2.0 * std::clamp(p.navBrake, 0.05, 1.0) * p.navAccel * std::max(0.0, dist - 0.5 * p.navReach))));
		out.move = Move::kNav;
		out.vDes = dir * sp;
		out.aim = aim;
		out.aimVel = {};
		out.accel = p.navAccel;
		out.vMax = vcap;
		out.face = FlatDir(a_goalChest - f.meChest);
		out.planKind = 5;  // 0-3 plan kinds, 4 nav, 5 push (logs)
		++_st.pushFrames;
		_pushAt = _now;
	}

	// ------------------------------------------------------------------------------------------- movement
	void Brain::FarLegExtras(const Facts& f, const Params& p, IWorld& w, Out& out, const Vec3& a_aim)
	{
		if (f.interiorCell) {  // ROUND3 SPEED (S5): a porch / eave keeps the look-ahead and the low-clearance climb
			_lookHit = false;
			_lowClear = false;
			return;
		}
		// look-ahead along v, never past the aim (a steep dive at a target below would see the ground behind it)
		const double sp = Length(f.meVel);
		const double len = std::min(sp * p.lookAhead + p.lookPad, Length(a_aim - f.meChest) - p.aimBack);
		if (sp > 300.0 && len > p.lookPad && CanCast(1)) {
			const Vec3 dir = f.meVel / sp;
			_lookHit = !RClear(w, f.meChest, f.meChest + dir * len);
		} else {
			_lookHit = false;
		}
		if (_now - _clearAt >= p.clearEvery - kTiny && CanCast(1)) {
			_clearAt = _now;
			Vec3 fl{};
			_lowClear = RFloor(w, f.meFeet + Vec3{ 0, 0, 1 }, p.minClear + 1, fl);
		}
		const bool farFromAim = Length(a_aim - f.meChest) > p.handR;
		out.climb = (_lowClear && farFromAim) || _lookHit;
		if (out.climb) out.vDes.z = std::max(out.vDes.z, p.climbV);
	}

	bool Brain::FollowPlan(const Facts& f, const Params& p, const Ctx&, IWorld& w, Out& out, const Cand* a_t, const Vec3& a_goalFeet,
		const Vec3& a_goalChest, double a_goalHeight, bool a_forReturn)
	{
		const std::uint32_t id = a_t ? a_t->id : 0;
		const double        vcap = f.indoors ? std::min(p.huntVMax, p.indoorMaxSpeed) : p.huntVMax;
		bool                need = _planFor != id || _planForReturn != a_forReturn || _plan.kind == Plan::kDirect || _planAt < -1e8;
		if (!need) {
			const double age = _now - _planAt;
			if (_plan.kind == Plan::kFail) need = age >= p.replan - kTiny;
			else {
				need = (_lookHit && age >= p.replan - kTiny) || (f.blockedFor >= p.replanBlocked && age >= 2.0 * p.replan) ||
				       Length(Flat(a_goalFeet - _planGoal)) > 300.0 || (_exhaustedSince >= 0 && _now - _exhaustedSince >= p.replan && age >= p.replan) ||
				       (_plan.kind == Plan::kSide && _wpIdx >= _plan.n);
			}
		}
		if (need) {
			_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetPlan));
			PlanIn in;
			in.meFeet = f.meFeet;
			in.meChest = f.meChest;
			in.tFeet = a_goalFeet;
			in.tChest = a_goalChest;
			in.tHeight = a_goalHeight;
			in.exterior = !f.interiorCell;  // ROUND3 SPEED (S5): under a porch the up-ray lowers / blocks the crossing itself
			in.cellCheck = !f.interiorCell;
			in.playerFeet = f.playerFeet;
			in.uGrids = f.uGrids;
			in.maxRays = std::min(static_cast<int>(p.planRays), _rayCap - _rays);
			in.forReturn = a_forReturn;  // FINISH A2 fix lane 5 (brain.skyMidH / skyRampDeg: returns only)
			_plan = PlanPath(in, p, w);
			_rays += _plan.rays;
			++_st.plans;
			if (_plan.mid) ++_st.planMids;  // FINISH A2 fix lane 5 (brain.skyMidH / skyRampDeg)
			if (_plan.ramp) ++_st.planRamps;
			if (_plan.rampNo) ++_st.planRampNo;
			_planFor = id;
			_planForReturn = a_forReturn;
			_planAt = _now;
			_wpIdx = 0;
			_planGoal = a_goalFeet;
			_lookHit = false;
			if (_plan.kind == Plan::kFail) {
				++_st.planFails;
				if (_noPathSince < 0) _noPathSince = _now;
			} else {
				_noPathSince = -1;
				_exhaustedSince = -1;
			}
		}
		out.planKind = _plan.kind;
		out.planH = _plan.H;
		out.face = FlatDir(a_goalFeet - f.meFeet);
		out.accel = p.huntAccel;
		out.vMax = vcap;
		if (_plan.kind == Plan::kFail || _plan.n == 0) {
			out.move = a_forReturn ? Move::kReturn : Move::kHold;
			out.vDes = {};
			out.aim = f.meChest;
			return false;
		}
		// advance: a pure climb point (above him) is reached by height, the others by distance
		while (_wpIdx < _plan.n) {
			const Vec3& q = _plan.wp[_wpIdx];
			const bool  climbPt = Length(Flat(q - f.meChest)) < p.wpReach && q.z > f.meChest.z;
			const bool  reached = climbPt ? f.meChest.z >= q.z - p.wpReach : Length(q - f.meChest) <= p.wpReach;
			if (!reached) break;
			++_wpIdx;
		}
		Vec3 wp;
		if (_wpIdx >= _plan.n) {
			if (_exhaustedSince < 0) _exhaustedSince = _now;  // at the last waypoint and still hidden
			wp = _plan.wp[_plan.n - 1];
		} else {
			wp = _plan.wp[_wpIdx];
		}
		const Vec3   e = wp - f.meChest;
		const double dist = Length(e);
		double       sp = std::clamp(p.overTopGain * dist, p.overTopMinV, vcap);
		if (_wpIdx >= _plan.n) sp = std::min(p.overTopGain * dist, vcap);  // hold at the last waypoint
		out.vDes = dist > 1e-3 ? e * (sp / dist) : Vec3{};
		out.move = a_forReturn ? Move::kReturn : Move::kOverTop;
		out.aim = wp;
		out.aimVel = {};
		return true;
	}

	void Brain::ChooseMove(const Facts& f, const Params& p, const Ctx& cx, IWorld& w, Out& out, int a_sel, bool a_vis)
	{
		const Cand& c = f.cands[a_sel];
		const Info& in = _info[a_sel];
		_noPathChase = false;  // NoNavHidden sets it again
		out.target = c.id;
		out.tier = in.tier;
		out.cls = c.cls;
		out.visible = a_vis;
		out.pin = in.pin;
		const double d = Length(c.chest - f.meChest);
		out.dist = d;
		out.face = FlatDir(c.feet - f.meFeet);
		out.mayAttack = a_vis && d <= p.swingRange && !f.vatsPlayback && !f.killMove;
		const double vcap = f.indoors ? std::min(p.huntVMax, p.indoorMaxSpeed) : p.huntVMax;
		bool         farLeg = false;
		// BRAIN2 (INTEGRATOR, shadow review SR1-4): indoors, sight flickers for a few frames at door frames, pillars and the
		// victim's own ragdoll. Dropping into the AI walk on the first hidden frame was the stop-go crawl (REQ_USER_0105 #6).
		// A near target seen within brain.chaseAfter s keeps the near approach; one hidden longer (or never seen) is chased
		// by the engine AI as before (K9).
		const bool flicker = !a_vis && f.indoors && in.m != nullptr && d <= p.handR && _now - in.m->visTrueAt <= p.chaseAfter + kTiny;
		// ROUND3 REACH: a visible target is flown at straight only while that segment is clear of movement geometry (glass,
		// railings, windows pass sight but stop his capsule): two blocked checks in a row -> the navmesh leg.
		const bool navOn = p.nav >= 0.5 && w.NavAvailable();
		// ROUND3 fix 2 (in game R1, QASmoke hidden corner: "NAV|stage -1->0 why=nav" 8x and the AI chase speed on/off 8x in
		// 1.5 s, still 1.6 s on a valid plan): sight flickers at a corner (one of the three sight rays passes a gap or a
		// door frame) while the straight line stays blocked, and every visible frame reset the chain (stage -1) and flew
		// him straight at the wall again. Now a flicker to hidden keeps the blocked count, and while the chain runs a
		// visible frame leaves it only after a straight-line check made during the chain came back clear.
		// Rollback: brain.navChainHold = 0.
		const bool hold = p.navChainHold >= 0.5;
		const bool chainOn = navOn && hold && _stage >= 0;
		if (navOn && a_vis && d > p.navDirectMin) {
			const bool due = _now - _directAt >= p.navDirectEvery - kTiny || (chainOn && _directAt < _chainAt);
			if (due && CanCast(1)) {
				_directAt = _now;
				const bool clear = RClear(w, f.meChest, c.chest - Normalize(c.chest - f.meChest, { 1, 0, 0 }) * p.aimBack);
				_directBlocked = clear ? 0 : _directBlocked + 1;
				(clear ? _directClearAt : _directBlockAt) = _now;
			}
		} else if (!hold || a_vis || !navOn) {
			_directBlocked = 0;  // in sight within navDirectMin: straight anyway (a hidden frame keeps the count)
		}
		bool blockedVis = navOn && a_vis && _directBlocked >= 2;
		if (chainOn && a_vis && !blockedVis && d > p.navDirectMin && !(_directClearAt >= _chainAt && _directClearAt >= _directBlockAt)) {
			blockedVis = true;  // seen, but no clear straight line since the chain started: stay on the chain
			++_st.chainKeeps;
		}
		// ROUND4 (in game R3, museum: 15 s hovering 300 u above a foe below a balcony, 76 swings 0 hits, 5 give-ups): a
		// seen target he cannot get at - the straight approach is stuck (the L4D2 stuck law), or he has swung for
		// swingNoReach s without a hit at a target out of vertical reach - goes to the stage chain (a navmesh path round
		// the balcony / down the stairs, then the AI walk and the blink) for visStuckChain s, instead of the stuck give-up
		// and a 2.5 s ignore loop. No swings meanwhile. Rollback: brain.visStuckChain = 0, brain.swingNoReach = 0.
		{
			// FINISH lane 6 A_big_burst (brain.bigNoReachOff; K07_ANALYSIS row 4, Swan 00056B7B: soak 20261025-025802 dt1 03:00:16-20 'd=164-239
			// vis=true may=false move=N' for 4 s at its feet after its rock had killed the player, 20261024-181340 18:16:12-15 'd=352 / 213
			// may=false', no swing): a big body's chest is 250-330 u up, so at its feet the chest distance d never gets under approachMin
			// and dzReach is over swingNoReachDz - once his capsule pressed against its body for swingNoReach s this balcony rule stopped
			// his swings and the stuck chain walked him round it on the navmesh. His reach on a big body spans its feet to its chest
			// (the runtime's contact axis runs feet + 20 .. aim + 35, punch.big.reachK widens the fist tube): the vertical excess is
			// measured against that span and 'pressed against it' against the nearest point of it. 0 = off (bit exact)
			const double meZ = (f.interiorCell || f.lowCeiling) && f.haveSim && sp::math::IsFinite(f.simChest) ? f.simChest.z : f.meChest.z;
			const double dzOld = std::abs(c.chest.z - meZ);
			double       dzReach = dzOld, dReach = d;
			if (p.bigNoReachOff >= 0.5 && c.cls == Cls::kBig && sp::math::IsFinite(c.feet) && sp::math::IsFinite(c.chest)) {
				const double zLo = std::min(c.feet.z, c.chest.z), zHi = std::max(c.feet.z, c.chest.z);
				dzReach = std::max(0.0, std::max(zLo - meZ, meZ - zHi));
				const Vec3   lo{ c.feet.x, c.feet.y, zLo }, ax{ 0.0, 0.0, zHi - zLo };
				const double l2 = Dot(ax, ax);
				const double u = l2 > 1e-9 ? std::clamp(Dot(f.meChest - lo, ax) / l2, 0.0, 1.0) : 0.0;
				dReach = Length(f.meChest - (lo + ax * u));
				if ((dzOld > p.swingNoReachDz || d > p.approachMin + 100.0) && !(dzReach > p.swingNoReachDz || dReach > p.approachMin + 100.0)) ++_st.bigReachKeeps;  // frames
			}
			bool noReach = false;
			if (out.mayAttack && dReach <= 600.0 && p.swingNoReach > 0) {
				if (_noReachSince < 0) _noReachSince = _now;
				noReach = _now - _noReachSince >= p.swingNoReach - kTiny && dzReach > p.swingNoReachDz && (f.bodyContact || _stuck.t > 0);
			} else {
				_noReachSince = -1;
			}
			if (noReach) {
				out.mayAttack = false;
				++_st.noReachStops;  // frames
			}
			// (not pressed against it while swinging: farther than approachMin + 100, or out of vertical reach)
			const bool stuckVis = a_vis && _stuck.Stuck(p) && (dReach > p.approachMin + 100.0 || dzReach > p.swingNoReachDz);
			if (navOn && a_vis && p.visStuckChain > 0 && (stuckVis || noReach) && _now >= _stuckChainUntil) {
				_stuckChainUntil = _now + p.visStuckChain;
				++_st.stuckChains;
			}
			// ROUND5 A2 (gate A1, museum at 60 fps: hovering 120-250 u from the raider for 16 s, sight flickering, 8 swings
			// 0 hits - neither stuck nor out of vertical reach): within swingNoHitR of the foe for swingNoHit s with no hit
			// landing (from the moment he got near, or the last hit) means something is between: the reach chain takes it for
			// visStuckChain s (the navmesh round it, the push, the blink). brain.swingNoHit 0 = off.
			if (p.swingNoHit > 0 && navOn) {
				if (d <= p.swingNoHitR) {
					if (_nearSince < 0) _nearSince = _now;
				} else if (d > p.swingNoHitR + 100.0) {
					_nearSince = -1;
				}
				// ROUND5 B (user 21:10: "stuck in a wall ... like 3 seconds"; live 21:03:53-55 sat 1.5 s at d 89-105 with 'sim held
				// at a wall'): pressed against something (bodyContact) within swingNoHitFastR, the hand-over comes after
				// swingNoHitFast s instead of swingNoHit (still only when something is between: hidden or a blocked line).
				double noHitT = (p.swingNoHitFast > 0 && f.bodyContact && d <= p.swingNoHitFastR) ? std::min(p.swingNoHit, p.swingNoHitFast) : p.swingNoHit;
				// ROUND5 D1 (user: "iffy in buildings ... stuck in a wall ... like 3 seconds"; live 03:42:16 and 03:42:45: 8.3 s
				// and 8.8 s at d 70-97, visible, 36-68 swings, every contact / lunge line from his SIM body blocked while the
				// capsule's line below was clear, so this hand-over never fired): the runtime's own clock of blocked contact
				// lines counts as "something between", and after contactBlockedNoHit s of it the reach chain takes over.
				const bool contactBlk = p.contactBlockedChain > 0 && f.contactBlockedFor >= p.contactBlockedChain - kTiny;
				if (contactBlk) noHitT = std::min(noHitT, p.contactBlockedNoHit);
				// s10 fix lane 2 (brain.noHitHurt; s10 gate item 5): near a foe that is HURTING the player the hand-over comes after
				// noHitHurt s (still only when something is between)
				const bool hurtNH = p.noHitHurt > 0 && c.attackingPlayer && c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny;
				if (hurtNH) noHitT = std::min(noHitT, p.noHitHurt);
				// explore m1 fix lane 3 (brain.noHitPin; xp_m1d Mass Fusion roof, run 20261025-100856-xm1d-m1fourth-s13c 10:13:31.4-36.3 on
				// FF001730 and 36.7-43.3 on FF001722: melee raiders beating the player every ~1 s, he 97-260 u from them, seen, 30-38 swings
				// 0 hits a window, 'around none ... (a wall between)' x3, FB latched - the roof's vents between - and no reach chain in 5-7 s;
				// the one chain that started, at 36.25, flew him round in 0.45 s and the kill followed): a PINNER (hit the player within
				// attackerValid, at it) was skipped by this hand-over and by the around-none one below - and its own pin blink waits for
				// noProgress 2.5 s and is refused outdoors. Now it counts like any foe hurting the player (noHitHurt / contactBlocked)
				const bool pinNH = in.pin && p.noHitPin >= 0.5;
				if (_nearSince >= 0 && _now - std::max(_nearSince, _tHitAt) >= noHitT - kTiny && _now >= _stuckChainUntil && (!in.pin || pinNH) && !f.vatsPlayback &&
				    !f.killMove) {
					// only when something is between (hidden now, or the straight line is blocked): a foe in the open that is
					// merely hard to hit (a bloatfly darting about) keeps the approach
					bool between = !a_vis || contactBlk || (CanCast(1) && !RClear(w, f.meChest, c.chest));
					// s10 fix lane 2 (brain.noHitSimApart; s10 gate item 5, hf 1080u 16:19:19.6-22.6: his capsule 45-54 u from the caged
					// shooter with a clear line, his sim torso - the fist - 200-900 u away behind the cage wall, 2.9 s without a hit):
					// the torso apart from the capsule AND its own line to the foe blocked is something between too (apart in the
					// open - a dash ahead of the capsule - is not)
					if (!between && p.noHitSimApart > 0 && f.haveSim && sp::math::IsFinite(f.simChest) && Length(f.simChest - f.meChest) > p.noHitSimApart &&
					    CanCast(1) && !RClear(w, f.simChest, c.chest)) {
						between = true;
						++_st.noHitSimChains;
					}
					if (between && contactBlk) ++_st.contactBlockedChains;
					if (between) {
						_stuckChainUntil = _now + p.visStuckChain;
						++_st.noHitChains;
						if (hurtNH && noHitT < p.swingNoHit - kTiny) ++_st.noHitHurtChains;
						if (in.pin) ++_st.m1PinNoHit;  // explore m1 fix lane 3 (only brain.noHitPin lets a pinner in)
					}
					_nearSince = _now;  // the next look after another swingNoHit s near it
				}
			}
			// s11 fix lane 2 (brain.hurtStallT; s11 gate GK1-rr dt1 08:10:37.9-41.2: the soak raider FF001280 hitting the player, SEEN,
			// flown at straight (move H, vDes 4500) - d 1665 -> 962 -> 766 -> 649 u in 3.3 s, ~300 u/s: he hung 500-700 u above it on
			// the far side of a deck he could see past but not fly through; the stuck / no-progress clocks never fired (d crept down);
			// the chain's navmesh 'air' leg took him down only at 41.0, after its second, killing hit): a seen foe hurting the player
			// beyond hurtStallMinD u that he closes on slower than hurtStallV u/s over hurtStallT s goes to the reach chain at once
			// ("hurting": it hit the player within hitMemory 5 s - a melee raider swings every 3-4 s: 37.06 / 40.68)
			// s12 fix lane 3 (brain.hurtStallBelow; s12 gate soak 225511 dt3 23:06:22.95-25.23: the burst raider FF0017FB that had just
			// beaten the player was 480 u BELOW him under the deck he stood on - d 645 -> 480-520 u, inside hurtStallMinD, 16 swings,
			// no hit for 2.3 s while the raider FF0017F7 walked up and took the player 0.71 -> 0.25; NAV had a 221 u way down): a hurter
			// more than hurtStallBelow u below his chest counts at any distance (the deck case; a free dive is never behind pace)
			const bool hsBelow = p.hurtStallBelow > 0 && f.meChest.z - c.chest.z > p.hurtStallBelow;
			// s13 fix round 3 lane 3 (brain.hurtStallPin; the s13r2 gate GQ dt 1080u 19:40:02.763-04.155: FF001261 and FF001264, the melee
			// raiders beating the player AT it, were pinners - hit the player within attackerValid, within threat u of it - and this rule
			// skipped pinners: picked at 02.763, seen, 793-809 u below him on a roof, his body blocked (noProg 0.80, stuckT 0.36), he flew
			// straight at it for 0.95 s until two blocked straight checks started the chain at 03.716 (its navmesh leg took him down in 0.44
			// s); meanwhile FF001262 beat the player 1.30 s. The pinner's own blink waits for noProgress 2.5 s (refused outdoors anyway), and
			// in all 26 gate runs this hand-over fired once - the attackers that matter stand at the player): a pinner counts too
			const bool hsPin = in.pin && p.hurtStallPin >= 0.5;
			if (p.hurtStallT > 0 && navOn && a_vis && (!in.pin || hsPin) && (d > p.hurtStallMinD || hsBelow) && _now >= _stuckChainUntil && c.attackingPlayer && c.lastHitPlayerAt > -1e8 &&
			    _now - c.lastHitPlayerAt <= std::max(p.d2DbHitWin, p.hitMemory) + kTiny) {
				// the pace anchor: (time, distance) of the last frame he was on pace (closer by hurtStallV x the time since); behind
				// pace for hurtStallT s = stalled
				if (_hsId != c.id || _now < _hsAt || _now - _hsAt > 3.0 * p.hurtStallT + 0.5 || d <= _hsD - p.hurtStallV * (_now - _hsAt)) {
					_hsId = c.id;  // on pace (or a fresh anchor: a new target, one left a while ago)
					_hsAt = _now;
					_hsD = d;
				} else if (_now - _hsAt >= p.hurtStallT - kTiny) {
					_stuckChainUntil = _now + std::max(0.5, p.visStuckChain);
					++_st.hurtStallChains;
					if (d <= p.hurtStallMinD) ++_st.hurtStallBelowChains;  // s12 fix lane 3 (only the below rule let it in)
					if (hsPin) ++_st.hurtStallPinChains;  // s13 fix round 3 lane 3 (a pinner: only hurtStallPin let it in)
					_hsId = 0;  // the next look after the chain's window
				}
			} else if (_hsId == c.id && !(a_vis && (d > p.hurtStallMinD || hsBelow))) {
				_hsId = 0;
			}
			// s12 fix r2 lane 2 (brain.aroundNoneChain; s12r1 gate GQ hf 120 054846 CZ.b 05:52:36.14-38.44: FF00112D SEEN through the
			// Combat Zone's walls at 128-305 u for 2.3 s, 'CONTACT|around none ... (a wall between: the reach chain)' at 37.06, 37.31
			// and 37.89, yet the hand-over waited for the no-hit clock and its "something between" ray - the chest-to-chest line was
			// clear (a gap / bars), so each look found nothing between and restarted the clock; FF00112D shot the player at 37.45, the
			// chain started only at 38.16 and found the way round in 0.3 s): the runtime's punch-spot search came back "none" for
			// this seen target within aroundNoneR u - every contact line from his body is blocked AND no spot over / round it can be
			// reached straight - so the reach chain (the navmesh round the wall, the push, the blink) takes it at once for
			// visStuckChain s. Once per search (the runtime retries after punch.aroundRetry; the chain window covers it). Only a STALL:
			// near it (swingNoHitR) for aroundNoneStall s without a hit - in the r1 / s12 runs 22 % of the "around none" reports were
			// followed by a hit within 0.25 s anyway (he had just arrived, or the foe stepped clear), those keep the straight approach.
			const bool anStall = _nearSince >= 0 && _now - std::max(_nearSince, _tHitAt) >= p.aroundNoneStall - kTiny;
			// (explore m1 fix lane 3, brain.noHitPin: a pinner too - xp_m1d 10:13:37.27 / 40.51 / 41.36 'around none FF001722 d=179-212')
			if (p.aroundNoneChain >= 0.5 && navOn && a_vis && (!in.pin || p.noHitPin >= 0.5) && f.aroundNoneId != 0 && f.aroundNoneId == c.id && f.aroundNoneAge >= 0 &&
			    f.aroundNoneAge <= 0.25 && d <= p.aroundNoneR && anStall && _now >= _stuckChainUntil && !f.vatsPlayback && !f.killMove) {
				_stuckChainUntil = _now + std::max(0.5, p.visStuckChain);
				_nearSince = _now;  // (the no-hit look starts over after the chain)
				++_st.aroundNoneChains;
				if (in.pin) ++_st.m1PinAround;
			}
			if (navOn && a_vis && _now < _stuckChainUntil && !blockedVis) blockedVis = true;
		}
		// explore m1 fix lane 3 (brain.closeDefendR; see CloseDefendCase): indoors, a foe hurting the player right at it that he cannot
		// touch goes to the chain's BLINK stage at once - the D2 defend blink (hurtNow: in view allowed, outside the budget; its landing
		// needs a clear path line to the foe) - instead of the navmesh / push walk first. Once per blink gap; never right after his own
		// blink at it; not while the Router / the shield own it (Route / Tele: CloseDefendCase is off with Tele 1 / the Warden)
		if (navOn && _stage >= -1 && _stage <= 3 && !(c.id == _cdId && _now >= _cdAt - kTiny && _now - _cdAt < 1.0 - kTiny) && CloseDefendCase(c, f, p) &&
		    _now - _lastBlinkAt >= std::min(p.navBlinkGap, p.d2DefendBlinkGap) - kTiny && !(f.route.on && (f.route.owns == c.id || f.route.none == c.id)) &&
		    !(p.blinkKeep >= 0.5 && c.id == _blinkKeepId && _now < _blinkKeepUntil)) {
			_cdId = c.id;
			_cdAt = _now;
			if (_stage < 0) {
				_chainAt = _now;
				_chainD0 = d;
			}
			_triedNav = _triedRay = _aiTried = true;
			if (_stage != 3) StageTo(3, "closeDef");  // (already at the BLINK stage - a push near it, a refused blink's wait: its blink now)
			_stuckChainUntil = std::max(_stuckChainUntil, _now + std::max(0.5, p.visStuckChain));
			blockedVis = true;
			++_st.m1CloseDef;
		}
		// FINISH lane A_combat_zone (brain.czDefendT / czDefendStep; CzDefendCase): the cage defend - a foe hurting the player that he has
		// got no closer to for czDefendT s, within closeDefendMeR u of him, goes to the chain's BLINK stage now, the same way as the
		// close-defend stage above (its blink at any distance, cdNow; a landing with a clear path line; the BLINK stage's gap / repeat
		// / budget-defend rules all stay). The no-progress clock runs every frame this target is asked (seen or hidden: a sight flicker
		// never restarts it) and starts over when he gets czDefendStep u closer, on another target, after a 0.25 s gap, after his blink.
		if (p.czDefendT > 0) {
			if (_czId != c.id || _now < _czSeen - kTiny || _now - _czSeen > 0.25 + kTiny) {
				_czId = c.id;
				_czBest = d;
				_czAt = _now;
				_czPressAt = -1e9;
			} else if (d < _czBest - std::max(1.0, p.czDefendStep) - kTiny) {
				_czBest = d;
				_czAt = _now;
			}
			if (f.bodyContact || f.blockedFor > 0) _czPressAt = _now;  // (held / pressed at geometry: the cage, not a free flight round it)
			_czSeen = _now;
			if (navOn && _stage >= -1 && _stage <= 3 && !(c.id == _cdId && _now >= _cdAt - kTiny && _now - _cdAt < 1.0 - kTiny) && CzDefendCase(c, f, p, d) &&
			    _now - _lastBlinkAt >= std::min(p.navBlinkGap, p.d2DefendBlinkGap) - kTiny && !(f.route.on && (f.route.owns == c.id || f.route.none == c.id)) &&
			    !(p.blinkKeep >= 0.5 && c.id == _blinkKeepId && _now < _blinkKeepUntil)) {
				_cdId = c.id;  // (the BLINK stage's close-defend lift: its blink may go at any distance)
				_cdAt = _now;
				if (_stage < 0) {
					_chainAt = _now;
					_chainD0 = d;
				}
				_triedNav = _triedRay = _aiTried = true;
				if (_stage != 3) StageTo(3, "czDefend");
				_stuckChainUntil = std::max(_stuckChainUntil, _now + std::max(0.5, p.visStuckChain));
				blockedVis = true;
				_czAt = _now;  // (the next try after another czDefendT s without progress)
				++_st.czDefends;
			}
		}
		// s10 fix lane 2 (brain.hurtChainMem; s10 gate item 2, cz 16:21:48.04-49.49: the caged shooter FF000FC0 picked as the
		// hurter at 310 u, seen through the bars - flown at straight for 1.45 s (no hit) although its chain had reached the
		// blink stage there 2 s before; hf 1080u 16:19:15.39-16.81 the same, 1.4 s, then the defend blink): a foe hurting the
		// player whose reach chain is remembered at the same spot goes back to that chain at once (it resumes at the remembered
		// push / blink stage: the D2 defend blink, or with [R6] Tele 1 the shield). Once per pick; never right after his own blink.
		if (p.hurtChainMem >= 0.5 && navOn && a_vis && !blockedVis && in.m != nullptr && d > p.navDirectMin && _hurtMemId != c.id && !in.pin &&
		    c.attackingPlayer && c.lastHitPlayerAt > -1e8 && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny && ChainRemembered(p, c, *in.m) &&
		    !(p.blinkKeep >= 0.5 && c.id == _blinkKeepId && _now < _blinkKeepUntil)) {
			_hurtMemId = c.id;
			_stuckChainUntil = std::max(_stuckChainUntil, _now + std::max(0.5, p.visStuckChain));
			blockedVis = true;
			++_st.hurtChainMems;
		}
		// D2 (smoke CZ.b: 3.3 s to a raider behind the Combat Zone cage door - nav, door, blinkWait, blinkCap, blink - while
		// the others shot the player; D1's contact punches through bars once he is AT them, 6/6 in 0.12 s): a SEEN foe whose
		// straight line is blocked by something within d2BarsGap u of its own chest (bars, a rail, a door frame right at it:
		// one Cast from its chest toward him every d2BarsEvery s) is flown at straight - to the bars - instead of the
		// navmesh / door / blink chain. d2BarsNoHit s near it without a hit: this foe goes to the chain for 3 s.
		if (p.d2Bars >= 0.5 && navOn && a_vis && blockedVis && d <= p.d2BarsR && in.m != nullptr && _now >= in.m->barsFailUntil && !in.pin) {
			if (_barsId != c.id) {
				_barsId = c.id;
				_barsOk = false;
				_barsAt = -1e9;
				_barsSince = -1;
			}
			if (_now - _barsAt >= p.d2BarsEvery - kTiny && CanCast(1)) {
				_barsAt = _now;
				double       fr = -1;
				const double L = Length(f.meChest - c.chest);
				++_rays;
				const bool hit = w.Cast(c.chest, f.meChest, fr);
				const bool was = _barsOk;
				_barsOk = hit && fr >= 0 && fr * L <= p.d2BarsGap;
				if (_barsOk && !was) ++_st.d2Bars;
			}
			if (_barsOk) {
				const bool nearBars = d <= p.d2BarsGap + 160.0;
				if (nearBars && _barsSince < 0) _barsSince = _now;
				if (!nearBars) _barsSince = -1;
				if (_barsSince >= 0 && _now - std::max(_barsSince, _tHitAt) >= p.d2BarsNoHit - kTiny) {
					in.m->barsFailUntil = _now + 3.0;
					++_st.d2BarsFails;
					_barsOk = false;
					_barsSince = -1;
				} else {
					blockedVis = false;  // straight at it: the runtime's fist-sight contact punches through the bars
					_stuckChainUntil = -1e9;
				}
			}
		}
		// DPREP-FOLLOW (GATE_B fail 2): right after his own blink (brain.blinkKeep) a seen target within blinkAttackR is
		// attacked straight - the blink spot was chosen with a clear line to it - never handed to the reach chain (which
		// resumed at the BLINK stage and gave up "unreach" at d 106). A hidden one goes through a fresh chain (navmesh first).
		if (p.blinkKeep >= 0.5 && a_vis && c.id == _blinkKeepId && _now < _blinkKeepUntil && d <= p.blinkAttackR) {
			blockedVis = false;
			_stuckChainUntil = -1e9;
			out.blinkAttack = true;
			++_st.blinkAttacks;  // frames
		}
		// FINISH A2 lane 3 (brain.windowTrapT / windowTrapHold; the finA gate's F14: the r4m R3_HOUSE foe in the Sanctuary house 1650-1800 u
		// out, 5.5 s from pick to kill - 'NAV|stage -1->0' 5x, move N A N N A at 350-410 u, 'd1 sk' straightKeep 122 frames, FB latched;
		// 8 of the 34 recorded r4m runs since 10-16 show it: 5.5 / 5.6 / 6.6 / 14.3 / 14.7 s / never x3): the straight line from his chest to its chest
		// passes a window / a gap in the boards (the walls ray and the sight rays go through, his body does not), so every seen frame
		// with that line clear took him off the navmesh leg to the door and flew him straight at the wall again, and each time the
		// chain started over at stage 0. Now: flying straight at it (no chain), the straight line clear, his body held for windowTrapT s
		// with no progress toward it and more than approachMin + 100 u from it = the line is a TRAP - the reach chain keeps this target
		// for windowTrapHold s (no straight flight, no straightKeep / flicker restart while it holds; at it, or right after his own
		// blink, he flies straight as before). 0 = off (bit exact)
		// Round 2 (the verifier of round 1): the in-game tries between two chain restarts last 0.2-0.6 s and the nav leg he just left
		// keeps _progAt fresh for navNoProg s, so "held windowTrapT s with no _progAt" could miss every try. Now the trap sums its own
		// HELD frames: his last frame flew straight at THIS target (Plan::kDirect, move A / H), the straight line is clear
		// (_directBlocked 0), his body is held (FB blockedFor >= 0.05 s) and he is not closing in (his velocity toward it < 150 u/s) -
		// over the tries, forgotten 2 s after the last held frame or on another target; windowTrapT s of them = the trap. The Combat
		// Zone cage (bars his thin path rays pass on some frames, his body never: s10 CZ-*) is the same shape and is meant: from the
		// bars he cannot punch a foe more than approachMin + 100 u inside, the chain's navmesh / door / blink stages can.
		if (p.windowTrapT > 0 && navOn && in.m != nullptr && c.id == _target && !out.blinkAttack && d > p.approachMin + 100.0) {
			constexpr double kWtForget = 2.0, kWtHeldFb = 0.05, kWtCloseV = 150.0;
			if (_wtId != c.id || _now < _wtAt - kTiny || _now - _wtAt > kWtForget + kTiny) {
				_wtId = c.id;
				_wtHeld = 0;
				_wtAt = _now;
			}
			const Vec3 toIt = Normalize(c.chest - f.meChest, { 1, 0, 0 });
			const bool held = _wtStraight == c.id && _stage < 0 && !blockedVis && _directBlocked == 0 && f.blockedFor >= kWtHeldFb - kTiny &&
			                  sp::math::IsFinite(f.meVel) && Dot(f.meVel, toIt) < kWtCloseV;
			if (held) {
				_wtHeld += std::clamp(f.dt, 0.0, 0.1);
				_wtAt = _now;
			}
			if (_wtHeld >= p.windowTrapT - kTiny && _now >= in.m->windowTrapUntil) {
				in.m->windowTrapUntil = _now + std::max(0.5, p.windowTrapHold);
				_wtHeld = 0;
				++_st.windowTraps;
			}
			if (_now < in.m->windowTrapUntil) {
				blockedVis = true;  // (also on a hidden frame: no straightKeep / flicker / farFresh straight flight)
				++_st.windowTrapFrames;
			}
		}
		if (blockedVis && d > p.approachMin + 100.0) out.mayAttack = false;  // no swings at a foe behind glass / round a corner
		// ROUND5 D1 (r4m indoor arena: 'NAV stage -1->0 why=nav' 15 times in 1.5 s on one raider seen through a doorway, 3.4 s
		// for 1250 u; the user's 03:41:03 "chain restarts every ~0.1 s instead of flying in"): beyond handR the flicker rule
		// does not apply, so every hidden frame after a visible one restarted the reach chain. Now a target seen within
		// straightHold s, with the last straight-line check clear and recent, keeps the straight flight through the flicker.
		// 0 = round 5 C.
		const bool straightKeep = p.straightHold > 0 && !a_vis && navOn && _stage < 0 && in.m != nullptr && _now - in.m->visTrueAt <= p.straightHold + kTiny &&
		                          _directClearAt >= _directBlockAt && _now - _directClearAt <= p.straightHold + p.navDirectEvery + kTiny;
		if (straightKeep) ++_st.straightKeeps;  // frames
		// R6 step 6 FIX (visual gate S3, 60 m: the foe was hidden on the pick frame - terrain between - so the sky chain
		// started at once and he rose straight up out of view; 1 s later it was seen at 1662 u and the chain kept him up
		// there): a foe hidden beyond farFreshDirect u, outdoors, no chain yet, is flown at
		// straight (the low cruise: terrain clearance minClear, the look-ahead climb over what is in the way) while the sight
		// rays catch up - TABS skims low at it - for at most farFreshFor s per foe; then the chain as before. 0 = off.
		const bool farFresh = p.farFreshDirect > 0 && !a_vis && navOn && _stage < 0 && !f.interiorCell && !f.lowCeiling && d > p.farFreshDirect &&
		                      in.m != nullptr && in.m->farFreshT < p.farFreshFor - kTiny;
		if (farFresh) {
			++_st.farFreshFrames;
			in.m->farFreshT += std::clamp(f.dt, 0.0, 0.1);  // per foe (the Warden's re-picks never renew it)
		}
		if ((a_vis || (flicker && !chainOn) || straightKeep || farFresh) && !blockedVis) {
			_noPathSince = _exhaustedSince = -1;
			if (_planFor == c.id && !_planForReturn) _planFor = 0;
			_stage = -1;  // ROUND3 REACH: seen with a clear line, the chain starts over next time
			if (!farFresh || a_vis) {  // R6 step 6 FIX: the fresh straight flight at a hidden foe keeps its hidden clocks
				_hiddenT = 0;
				_hiddenTotal = 0;  // ROUND4
			}
			_nav.dead = _nav.island = _nav.doorDead = false;
			_nav.doorHave = false;
			_nav.doorTries = 0;
			out.planKind = Plan::kDirect;
			if (d <= p.handR) {
				const double vmax = f.indoors ? std::min(p.approachMax, p.indoorMaxSpeed) : p.approachMax;
				out.move = Move::kApproach;
				// ROUND3 REACH 3.5: in a low room the sim rides the ceiling band while the capsule sits on the floor; steer the
				// SIM to the target's chest height (the vertical error from the sim, not from his capsule)
				Vec3 meF = f.meFeet, meC = f.meChest;
				if ((f.interiorCell || f.lowCeiling) && f.haveSim && sp::math::IsFinite(f.simChest)) {
					const double dz = f.simChest.z - f.meChest.z;
					meF.z += dz;
					meC.z += dz;
				}
				// ROUND5 B: the TABS ram arrival (brain.ram 1) instead of the stand-off at approachMin
				out.vDes = p.ram >= 0.5 ? RamVDes(meF, c.feet, c.vel, vmax, p, f.meVel) : ApproachVDes(meF, meC, c.feet, c.chest, vmax, p, f.meVel);
				// ROUND3 REACH 3.5 stand-off breaker: in reach, swinging, nothing lands for standOffAfter s -> close in on
				// the chest (approachMin 0) for standOffFor s, then again
				if (p.standOff >= 0.5 && out.mayAttack && d < 300) {
					if (_noHitSince < 0) _noHitSince = _now;
					const double t = _now - _noHitSince;
					if (t >= p.standOffAfter - kTiny) {
						const Vec3   err = c.chest - meC;
						const double el = Length(err);
						if (el > 1) out.vDes = err * (std::min(p.approachGain * el, vmax) / el);
						if (t >= p.standOffAfter + p.standOffFor - kTiny) _noHitSince = _now;
					}
				} else {
					_noHitSince = -1;
				}
				out.aim = c.chest;
				out.aimVel = c.vel;
				out.accel = p.flyAccel;
				out.vMax = vmax;
			} else {
				out.move = Move::kHunt;
				farLeg = true;
				const Vec3 dir = Normalize(c.chest - f.meChest, { 1, 0, 0 });
				out.aim = c.chest - dir * p.aimBack;
				out.aimVel = c.vel;
				const CruiseCfg cc{ vcap, p.huntAccel, std::min(p.approachMax, vcap), p.handR, p.huntFF, p.huntBrake };
				out.vDes = CruiseVDes(f.meChest, out.aim, out.aimVel, cc);
				out.accel = p.huntAccel;
				out.vMax = vcap;
			}
		} else {
			_noHitSince = -1;
			// R6 step 9 ([R6] Route 1): the runtime's Router drives this target with TRAVEL dashes (the chain is suspended), or
			// it found no way at all (the defend blink stage or the give-up at once)
			if (f.route.on && f.route.owns != 0 && f.route.owns == c.id) RouteHidden(f, p, out, c);
			else if (f.route.on && f.route.none != 0 && f.route.none == c.id && navOn) RouteNone(f, p, cx, w, out, c, in);
			else if (navOn) ReachHidden(f, p, cx, w, out, c, in);  // ROUND3 REACH: NAV -> RAY -> AI -> BLINK -> give up
			else NoNavHidden(f, p, cx, w, out, c);            // ROUND3 SPEED (S5): sky / side path, AI chase in real interiors
			farLeg = out.move == Move::kNav || out.move == Move::kOverTop;
		}
		// No terrain-clearance climb on a navmesh leg: it flies on purpose at floor + navHover, so the climb would lift him
		// into ceilings and door lintels.
		// DPREP-FOLLOW (tabs-spec G9): the straight move's line verdict for the runtime's TABS dash chain indoors / beyond
		// body.tabsNearR - seen, flown straight, and the 0.15 s straight-line check (navDirectEvery) clear or within
		// navDirectMin. Without the navmesh there is no check: false (round 5 B keeps its outdoor chain without it).
		out.directClear = navOn && a_vis && !blockedVis && _directBlocked == 0 && (out.move == Move::kApproach || out.move == Move::kHunt);
		if (farLeg && out.move != Move::kNav) FarLegExtras(f, p, w, out, out.aim);
		if (farLeg && !_wasFarLeg && f.meOnGround) out.launch = true;
		_wasFarLeg = farLeg;
		_aiChasing = out.move == Move::kAiChase;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		out.hunt = out.move == Move::kAiChase ? HuntState::kAiChase : (IsFar(in.tier) ? HuntState::kFar : HuntState::kLocal);
	}

	// ROUND3: the engine AI chase (Papyrus StartCombat once per target; the runtime raises his SpeedMult meanwhile)
	void Brain::AiChase(const Facts& f, const Params& p, Out& out)
	{
		out.move = Move::kAiChase;
		out.vDes = {};
		out.aim = f.meChest;
		out.accel = p.flyAccel;
		out.vMax = 0;
		out.planKind = Plan::kFail;
		if (!_chaseSent) {
			_chaseSent = true;
			out.chaseRequest = true;
			++_st.chases;
		}
	}

	// ROUND3 SPEED (S5): a hidden target without the navmesh. Outdoors (also under a porch / eave, where headroom made
	// f.indoors true and he walked at AI pace: live 17:15:46-50) it is flown at over the sky path / side detour. The
	// engine AI chase (K9) is kept for real interior cells and for a roofed exterior spot with no flight path.
	void Brain::NoNavHidden(const Facts& f, const Params& p, const Ctx& cx, IWorld& w, Out& out, const Cand& c)
	{
		bool flying = false;
		if (!f.interiorCell) flying = FollowPlan(f, p, cx, w, out, &c, c.feet, c.chest, c.height, false);
		// ROUND3 fix (review): nav off / self-off and no sky or side path -> the engine AI chase (it pathfinds through
		// doors) instead of hovering (kHold) until the blind / no-path give-up, also when he stands under open sky with
		// the foe inside a roofed exterior building. Only for a foe that fights or hit the group (a hidden idle one is
		// excluded anyway). Rollback: brain.noPathAiChase = 0 (AI chase only on a roofed spot).
		const bool fighting = c.combatWithGroup || _now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt }) <= p.hitMemory;
		const bool noPathChase = !flying && p.noPathAiChase >= 0.5 && fighting;
		const bool roofed = f.interiorCell || (!flying && f.indoors);
		if (roofed || noPathChase) AiChase(f, p, out);  // (ROUND5: the no-navmesh fallback keeps the engine AI's pathing)
		_noPathChase = !roofed && noPathChase;  // its no-progress limit is noPathGiveUp (2 s), not the K9 chaseNoProgress 6 s
	}

	// ROUND3 REACH: is the target under a roof (an up-ray from its chest, cached navCoverEvery s)
	bool Brain::TargetCovered(const Cand& c, Mem& m, const Params& p, IWorld& w)
	{
		if (_now - m.coverAt >= p.navCoverEvery - kTiny && CanCast(1)) {
			m.coverAt = _now;
			double fr = -1;
			++_rays;
			m.covered = w.Cast(c.chest, c.chest + Vec3{ 0, 0, p.navCoverProbe }, fr);
		}
		return m.covered;
	}

	// ROUND3 REACH: could the player see a_p (60-degree half-cone round the view, then a sight ray from the eye; no ray
	// budget = seen). Generous on purpose: a spot the third-person camera might see counts as seen (no blink).
	bool Brain::PlayerSees(const Facts& f, const Params& p, IWorld& w, const Vec3& a_p)
	{
		auto from = [&](const Vec3& a_eye) {
			const Vec3   d = a_p - a_eye;
			const double l = Length(d);
			if (l < 1e-3) return true;
			const double cs = Dot(d, f.playerView) / (l * std::max(1e-9, Length(f.playerView)));
			if (cs < std::cos(p.navViewDeg * sp::math::kDeg2Rad)) return false;  // outside the view cone
			if (!CanCast(1)) return true;
			return RClearTo(w, a_eye, a_p, 0, 16.0);  // walls hide him, glass does not
		};
		if (from(f.playerEye)) return true;
		// D2: the third-person camera sits behind and above the eye: a spot right behind the player may be in its view
		// (brain.d2CamBack 0 = the eye only, D1H)
		if (p.d2CamBack > 0) {
			const Vec3 v = Normalize(f.playerView, { 0, 1, 0 });
			const Vec3 cam = f.playerEye - v * p.d2CamBack + Vec3{ 0, 0, p.d2CamUp };
			if (from(cam)) return true;
		}
		return false;
	}

	// D2 (user 03:43:07 'blink 0017B855 reach d=1542' in the open, mid-fight; 03:52 16 blinks in 21 min): a blink of any
	// kind needs an interior cell (brain.d2BlinkExterior 0) and both ends out of the player's view (brain.d2BlinkSeen 0;
	// the landing is checked by every caller already). a_takeOffSeen: the caller's own answer for his chest (-> no ray).
	bool Brain::D2BlinkAllowed(const Facts& f, const Params& p, IWorld& w, const Vec3& a_landChest, bool a_takeOffSeen, bool a_defend)
	{
		// step-2 fixer (s2fDefendBlink): a defend blink while the player is below s2fDefendHp is allowed outdoors too
		const double s2fHp = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
		const bool   s2fExt = a_defend && p.s2fDefendBlink >= 0.5 && s2fHp < p.s2fDefendHp;
		if (p.d2BlinkExterior < 0.5 && !f.interiorCell && !s2fExt) {
			++_st.d2BlinkExt;
			return false;
		}
		if (!a_defend && p.d2BlinkBudget > 0 && D2BlinksInWindow(p) >= static_cast<int>(p.d2BlinkBudget)) {
			++_st.d2BlinkBudgetNo;
			return false;
		}
		if (p.d2BlinkSeen < 0.5 && !a_defend) {
			if (a_takeOffSeen || PlayerSees(f, p, w, f.meChest) || (IsFinite(a_landChest) && PlayerSees(f, p, w, a_landChest))) {
				++_st.d2BlinkSeen;
				return false;
			}
		}
		return true;
	}

	// D2 (brain.d2BlinkBudget): the blink ring (brain clock)
	int Brain::D2BlinksInWindow(const Params& p) const
	{
		int n = 0;
		for (double t : _d2BlinkAt)
			if (t > -1e8 && _now - t <= p.d2BlinkWindow) ++n;
		return n;
	}
	void Brain::D2NoteBlink()
	{
		double* oldest = std::min_element(std::begin(_d2BlinkAt), std::end(_d2BlinkAt));
		*oldest = _now;
	}

	// D2 (brain.d2ReturnBlink): an out-of-view landing near the player for a put-back - on the floor d2ReturnBlinkBack u
	// behind the player (then the back diagonals, then nearer), in the same room (clear path line from the player's chest
	// to the spot's chest), seen neither from the eye nor from the third-person camera. 2-3 rays per try, <= 6 tries.
	bool Brain::ReturnBlinkSpot(const Facts& f, const Params& p, IWorld& w, Vec3& a_feet)
	{
		const Vec3   v = FlatDir(f.playerView);
		const Vec3   fwd = LengthSq(v) > 0.25 ? v : Vec3{ 0, 1, 0 };
		const Vec3   right{ fwd.y, -fwd.x, 0 };
		const double chestUp = std::max(60.0, f.meChest.z - f.meFeet.z);
		struct Try
		{
			double back, side;
		};
		const double b = std::max(150.0, p.d2ReturnBlinkBack);
		const Try    tries[] = { { b, 0 }, { b * 0.8, b * 0.55 }, { b * 0.8, -b * 0.55 }, { b * 0.6, 0 }, { b * 0.45, b * 0.45 }, { b * 0.45, -b * 0.45 } };
		for (const Try& t : tries) {
			if (!CanCast(3)) return false;
			const Vec3 xy = f.playerFeet - fwd * t.back + right * t.side;
			Vec3       floor{};
			if (!RFloor(w, xy + Vec3{ 0, 0, 150 }, 400.0, floor)) continue;
			if (std::abs(floor.z - f.playerFeet.z) > 200.0) continue;
			const Vec3 chest = floor + Vec3{ 0, 0, chestUp };
			if (!RClear(w, f.playerChest, chest)) continue;  // same room, nothing between
			if (PlayerSees(f, p, w, chest)) continue;
			a_feet = floor;
			return true;
		}
		return false;
	}

	// ROUND3 REACH: fly the navmesh path to c. true = flying (out filled, move kNav). false = no usable path now.
	bool Brain::FollowNav(const Facts& f, const Params& p, IWorld& w, Out& out, const Cand& c)
	{
		const double chestUp = std::max(0.0, f.meChest.z - f.meFeet.z);
		NavState&    n = _nav;
		if (n.forId != c.id) {
			n = NavState{};
			n.forId = c.id;
			n.progAt = _now;
		}
		if (n.dead) return false;
		// ROUND4: hover in place (a door is opening), as a nav leg (the chain's still clock does not run)
		auto hold = [&]() {
			out.move = Move::kNav;
			out.vDes = {};
			out.aim = f.meChest;
			out.aimVel = {};
			out.accel = p.navAccel;
			out.vMax = 0;
			out.face = FlatDir(c.feet - f.meFeet);
			out.planKind = 4;
			return true;
		};
		if (_now < n.doorWaitUntil) return hold();
		bool exhausted = n.ok && n.idx >= static_cast<int>(n.wp.size());
		// ROUND4 (in game R3, Sanctuary house run 6: he stopped a few hundred u short of the island's door step, at the wall,
		// so "flown to its end" never fired and the stuck law gave up first): within navIslandReach of the door step counts
		if (!exhausted && n.ok && n.partial && !n.wp.empty() && n.idx >= static_cast<int>(n.wp.size()) - 1 &&
			Length(Flat(n.wp.back() - f.meChest)) <= p.navIslandReach)
			exhausted = true;
		// ROUND3 fix 2: a partial path (island goal) flown to its end: the NAV stage is done (the door AI / blink go on from
		// the door step); re-planning would give the same partial path
		if (exhausted && n.partial) {
			// ROUND4 (in game R3: the Sanctuary house door was never passed): at the door step, open the door(s) between him
			// and the target (native, closed + unlocked + not a load door), wait for it to swing, then fly through toward the
			// target for navDoorPassFor s (slowly, while the line ahead is clear); then a fresh plan from wherever he got to
			// (inside, the island is his own component). Rollback: brain.navOpenDoors = 0, brain.navDoorPass = 0.
			if (!n.islandDoorTried && (p.navOpenDoors >= 0.5 || p.navDoorPass >= 0.5)) {
				n.islandDoorTried = true;
				if (p.navOpenDoors >= 0.5) {
					const Vec3 to = f.meChest + ClampLen(c.chest - f.meChest, 600.0);
					const int  opened = w.OpenDoors(f.meChest, to, p.navDoorOpenR);
					n.doorOpenAt = _now;
					if (opened > 0) {
						_st.doorsOpened += static_cast<std::uint64_t>(opened);
						n.doorWaitUntil = _now + p.navDoorOpenWait;
						if (p.navDoorTrust > 0) n.trustUntil = n.doorWaitUntil + p.navDoorTrust;  // ROUND5 A2
					} else if (opened == 0 && w.LockedDoorsSeen() > 0 && SmashOnSegment(f, p, w, f.meChest, to)) {
						n.doorWaitUntil = _now + p.navDoorOpenWait;  // ROUND5: a locked door at the island's step, forced
					}
				}
				if (p.navDoorPass >= 0.5) {
					n.passUntil = std::max(_now, n.doorWaitUntil) + p.navDoorPassFor;
					n.passGoal = c.chest;
					++_st.doorPasses;
				}
				if (_now < n.doorWaitUntil) return hold();
			}
			if (_now < n.passUntil) {
				const Vec3   e = c.chest - f.meChest;
				const double dist = Length(e);
				const Vec3   dir = Normalize(e, { 1, 0, 0 });
				bool         clear = true;
				if (_now - n.pullAt >= p.navPullEvery - kTiny && CanCast(1)) {
					n.pullAt = _now;
					clear = RClear(w, f.meChest, f.meChest + dir * std::min(dist, 160.0));
				}
				if (clear && dist > 1.0) {
					out.move = Move::kNav;
					out.vDes = dir * std::min(p.navDoorV, std::max(p.navVMin, 3.0 * dist));
					out.aim = c.chest;
					out.aimVel = {};
					out.accel = p.navAccel;
					out.vMax = p.navDoorV;
					out.face = FlatDir(e);
					out.planKind = 4;
					return true;
				}
				n.passUntil = -1e9;  // blocked right ahead: the pass is over
			} else if (n.passUntil > -1e8) {
				// the pass ran its time: plan again from here (inside the house the goal is no island any more)
				n.passUntil = -1e9;
				n.partial = false;
				n.ok = false;
				n.replanNow = true;
				n.tryAt = -1e9;
				return hold();
			}
			n.dead = true;
			n.island = true;
			++_st.islands;
			return false;
		}
		// ROUND5 A2 (gate A1, museum upstairs at 60 fps, FF001017: "pts=2 len=19-62 goalOff=141 dz=-103..-115" - he hovered at
		// the end of the leg at d 149-163 for 2.6 s, re-planning the same 2-point path every 0.25 s, 3 swings 0 hits, then a
		// give-up): the foe stands off the navmesh (the leg's last floor point is more than navOffGoal u from its feet) and
		// the leg is used up (within navEndReach of its last corner). After navEndHold s the leg ends (offMesh): the chain
		// goes on to the push straight at it, then the blink. navOffGoal 0 = off.
		if (n.ok && !n.partial && !n.wp.empty() && n.floor.size() == n.wp.size() && p.navOffGoal > 0 && c.id != kReturnNavId) {
			const int  lastI = static_cast<int>(n.wp.size()) - 1;
			const bool atEnd = n.idx > lastI || (n.idx >= lastI && Length(n.wp[static_cast<size_t>(lastI)] - f.meChest) <= std::max(p.navEndReach, p.navReach));
			const bool off = Length(n.floor[static_cast<size_t>(lastI)] - c.feet) > p.navOffGoal;
			// once at the end it stays "at the end" (an overshoot past the last corner and the re-plan from there must not
			// restart the clock) until the foe moves away (navGoalMove) or the leg leads somewhere else
			const bool stay = n.endSince >= 0 && Length(Flat(c.feet - n.goal)) <= p.navGoalMove && Length(n.wp[static_cast<size_t>(lastI)] - f.meChest) <= 3.0 * p.navEndReach + p.navReach;
			if (off && (atEnd || stay)) {
				if (n.endSince < 0) n.endSince = _now;
				if (_now - n.endSince >= p.navEndHold - kTiny) {
					n.dead = true;
					n.offMesh = true;
					++_st.offMeshEnds;
					return false;
				}
			} else {
				n.endSince = -1;
			}
		}
		const bool bodyReplan = f.blockedFor >= 0.3 && _now - n.at >= 2.0 * p.navReplan;
		const bool need = !n.ok || n.replanNow || exhausted || Length(Flat(c.feet - n.goal)) > p.navGoalMove || bodyReplan;
		if (need && _now - n.tryAt >= p.navReplan - kTiny) {
			// A thin door can let the segment ray pass while the body stalls. This 0.3s replan runs before
			// navBodyBlocked's 0.5s escalation and renews its progress clock; repeated replans can therefore
			// starve both later door checks forever (B2L4-DOOR seed 3). Try the still-valid route's door first.
			// A failed/unsupported query changes no route output or progress; NavDoorFirst owns its rate limit.
			if (p.navDoorFirst >= 0.5 && bodyReplan && n.ok && !n.wp.empty() && Length(Flat(c.feet - n.goal)) <= p.navGoalMove &&
			    NavDoorFirst(f, p, w, std::min(n.idx, static_cast<int>(n.wp.size()) - 1))) return hold();
			n.tryAt = _now;
			IWorld::NavOut no;
			const bool ok = w.NavPath(f.meFeet, c.feet, c.id, p.navAgentR, no);
			// ROUND3 fix 7: the world built its snapshot this frame and moved the plan to the next one (no failure counted)
			if (!ok && no.why && std::string_view(no.why) == "defer") {
				n.tryAt = -1e9;
				++_st.navDefers;
				return false;
			}
			++_st.navPlans;
			if (n.ok) ++_st.navReplans;
			// ROUND4 fix 2 (in game R1, museum at 120 fps: a re-plan every 0.2-0.35 s while the body was blocked, each one
			// restarting the no-progress clock, so the leg never ended and the stuck law put him back): a re-plan for the same
			// goal from a valid plan keeps the clock, with at most navReplanGrace s of fresh time. navReplanGrace < 0: round 4.
			const bool sameGoal = n.ok && Length(Flat(c.feet - n.goal)) <= p.navGoalMove;
			const double progKeep = n.progAt;
			if (!sameGoal) n.movedAt = -1e9;  // cx2: no route-progress credit from a different goal
			n.at = _now;
			n.goal = c.feet;
			n.replanNow = false;
			// ROUND3 fix 2: a re-plan asked by a blocked segment keeps the mark (a second block there arms the door AI);
			// any other re-plan (goal moved, no progress, exhausted, stuck) clears it
			if (!n.blockReplan) n.segReplanned = false;
			n.blockReplan = false;
			n.partial = ok && no.partial;
			n.wp.clear();
			n.floor.clear();
			n.flags.clear();
			n.idx = 0;
			n.segChecked = -1;
			n.ok = ok && no.pts.size() >= 2;
			if (n.ok) {
				// ROUND5 (in game round 4 R2, MQ102 bloatflies in Sanctuary: outdoor navmesh legs at 3000 u/s ran at knee height
				// along fences, porches and clutter): outdoors the corners sit brain.navHoverOut u over the floor (a flyer, not
				// a runner); door portals, the last corner (the foe) and his tight spaces keep brain.navHover. A blocked high
				// corner still falls back to the low one (the segment check). navHoverOut <= navHover: round 4.
				const bool outdoors = !f.interiorCell && !f.lowCeiling;
				// ROUND5 A2 (gate A1, Sanctuary house at 120 fps: 4.8 s against 1.3 s in round 4 - the lifted corners took him
				// over the porch at +96 u, where he stalled 1.5 s with re-plans): near a goal under a roof (a foe inside a house)
				// the corners stay low (within navHoverOutNear u of it), as in round 4; foes in the open keep the lift.
				const auto goalMem = c.id != kReturnNavId ? _mem.find(c.id) : _mem.end();
				const bool goalCovered = goalMem != _mem.end() && goalMem->second.covered;
				for (size_t i = 1; i < no.pts.size(); ++i) {
					const std::uint8_t fl = i < no.flags.size() ? no.flags[i] : 0;
					const bool         door = fl != 0 || (i + 1 < no.flags.size() && no.flags[i + 1] != 0) || (i >= 1 && i - 1 < no.flags.size() && no.flags[i - 1] != 0);
					const bool         nearCovered = goalCovered && p.navHoverOutNear > 0 && Length(Flat(no.pts[i] - no.pts.back())) <= p.navHoverOutNear;
					const double       hov = (outdoors && !door && !nearCovered && i + 1 < no.pts.size()) ? std::max(p.navHover, p.navHoverOut) : p.navHover;
					n.floor.push_back(no.pts[i]);
					n.wp.push_back(no.pts[i] + Vec3{ 0, 0, hov + chestUp });
					n.flags.push_back(fl);  // ROUND4: door portals
				}
				// cx2: a same-goal return replan must not turn local oscillation into fresh route progress.
				n.best = p.retNavProgress >= 0.5 && c.id == kReturnNavId && sameGoal ? std::min(n.best, no.length) : no.length;
				n.progAt = (sameGoal && p.navReplanGrace >= 0) ? std::max(progKeep, _now - std::max(0.0, p.navNoProg - p.navReplanGrace)) : _now;
				// s4 fix lane 3: the spot of the last counted progress survives a same-goal re-plan (a new goal starts it here)
				if (!sameGoal || !n.progPosSet) {
					n.progPos = f.meChest;
					n.progPosSet = true;
				}
				// ROUND5 A2: a re-plan of a used-up leg for the same goal (the same short path again and again under the
				// balcony) gets no fresh time at all
				if (sameGoal && exhausted && p.navOffGoal > 0) n.progAt = progKeep;
			} else {
				++_st.navFails;
				if (++n.fails >= static_cast<int>(p.navMaxFails)) n.dead = true;
				return false;
			}
		}
		if (!n.ok || n.wp.empty()) return false;
		// advance: reached by distance (navReach: an early speed-scaled advance cut the wall corner at a door); string pull:
		// the one after is in a clear line (1 ray per navPullEvery)
		while (n.idx < static_cast<int>(n.wp.size()) && Length(n.wp[n.idx] - f.meChest) <= p.navReach) {
			++n.idx;
			n.segChecked = -1;
		}
		// (s13 fix round 3 lane 4: not on the brain.nearStallR leg - it exists because a clear ray lied to his body: a lip, a rail,
		// a canopy edge the thin ray slips through; the leg flies the navmesh corners)
		if (!_navNoPull && n.idx + 1 < static_cast<int>(n.wp.size()) && _now - n.pullAt >= p.navPullEvery - kTiny && CanCast(1)) {
			n.pullAt = _now;
			if (RClear(w, f.meChest, n.wp[n.idx + 1])) {
				++n.idx;
				n.segChecked = -1;
			}
		}
		const int last = static_cast<int>(n.wp.size()) - 1;
		int       i = std::min(n.idx, last);
		// verify the current segment once (1-3 rays). Blocked: hover 0; past a corner too early: back to the previous
		// waypoint; else one fresh plan from here; still blocked: the AI handles that door / prop for navDoorFor
		// ROUND4 fix 2 (in game R1, museum at 120 fps: the rays to the next corner were clear but his capsule was blocked for
		// 3+ s - a railing / door frame / low beam - and only the stuck put-back ended it): the mover's wall verdict for
		// navBodyBlocked s without nav progress counts as a blocked segment (the same re-plan -> doors -> door AI escalation;
		// the ray alternatives are skipped: they pass). navBodyBlocked 0 = rays only.
		const bool bodyBlocked = p.navBodyBlocked > 0 && f.blockedFor >= p.navBodyBlocked - kTiny && _now - n.progAt >= p.navBodyBlocked - kTiny &&
		                         _now - n.bodyBlockAt >= std::max(p.navBodyBlocked, p.navReplan) - kTiny;
		if (bodyBlocked) {
			n.bodyBlockAt = _now;
			++_st.navBodyBlocks;
		}
		bool d2Trust = false;  // D2: an open doorway flown on trust (below)
		(void)d2Trust;
		if (bodyBlocked || (n.segChecked != i && CanCast(1))) {
			n.segChecked = i;
			if (bodyBlocked || !RClear(w, f.meChest, n.wp[i])) {
				const Vec3 low = n.floor[i] + Vec3{ 0, 0, chestUp };
				if (!bodyBlocked && _now < n.trustUntil) {
					// ROUND5 A2 (gate A1, indoor arena at 60 fps: two doors opened, then "0->2 door", "doorBack", "door" again and
					// a blink, 7.4 s on one foe): he opened a door on this leg less than navDoorTrust s ago and its leaf may still
					// be swinging through the ray. He flies on at door speed; a body that stays blocked (navBodyBlocked) still
					// escalates. brain.navDoorTrust 0 = round 5 A.
					n.trustSeg = i;
					++_st.doorTrusts;
				} else if (!bodyBlocked && CanCast(1) && RClear(w, f.meChest, low)) {
					n.wp[i] = low;
				} else if (!bodyBlocked && i > 0 && CanCast(1) && RClear(w, f.meChest, n.wp[i - 1])) {
					n.idx = i = i - 1;
					n.segChecked = i;
				} else if (p.navDoorFirst >= 0.5 && NavDoorFirst(f, p, w, i)) {
					// FINISH B2 fix lane 4 (brain.navDoorFirst): a closed door on this blocked segment opens now, not after a re-plan
					return hold();
				} else if (!n.segReplanned) {
					n.segReplanned = true;
					n.replanNow = true;
					n.blockReplan = true;
					return false;  // re-planned from here on the next allowed frame (ReachHidden holds meanwhile)
				} else {
					// ROUND4 (in game R3: doors he flew at stayed shut; the door AI walked him away): a closed door on this
					// segment is opened natively (closed, unlocked, not a load door), then he hovers navDoorOpenWait s while it
					// swings and the segment is checked again; only when no door opened does the engine-AI door walk start
					if (p.navOpenDoors >= 0.5 && _now - n.doorOpenAt >= p.navDoorOpenGap - kTiny) {
						n.doorOpenAt = _now;
						const int opened = w.OpenDoors(f.meChest, n.wp[i], p.navDoorOpenR);
						if (opened > 0) {
							_st.doorsOpened += static_cast<std::uint64_t>(opened);
							n.doorWaitUntil = _now + p.navDoorOpenWait;
							if (p.navDoorTrust > 0) n.trustUntil = n.doorWaitUntil + p.navDoorTrust;  // ROUND5 A2
							n.segChecked = -1;
							n.segReplanned = false;
							// FINISH B2 fix lane 4 (brain.navDoorFirst): the door he opened gives the leg fresh no-progress time (the swing, then navNoProg)
							if (p.navDoorFirst >= 0.5 && n.progAt < n.doorWaitUntil) {
								n.progAt = n.doorWaitUntil;
								++_st.b2l4DoorFresh;
							}
							return hold();
						}
						// ROUND4 fix 2 (in game R1, museum upstairs at 120 fps: "DOOR|skip 000509AD (locked)", then the door AI
						// walked him away from the target and it was dropped): a locked door on this segment ends the navmesh
						// leg (the planner cannot route round it; the AI walk that follows counts only getting closer, then
						// the blink). navLockedDead 0 = the door AI as before.
						if (opened == 0 && w.LockedDoorsSeen() > 0 && SmashOnSegment(f, p, w, f.meChest, n.wp[i])) {
							// ROUND5: he forced it (a pickable lock): hover while it swings, then the same segment again
							n.doorWaitUntil = _now + p.navDoorOpenWait;
							n.segChecked = -1;
							n.segReplanned = false;
							return hold();
						}
						if (opened == 0 && p.navLockedDead >= 0.5 && w.LockedDoorsSeen() > 0) {
							n.dead = true;
							n.doorDead = true;
							n.locked = true;
							++_st.lockedDoors;
							return false;
						}
						// D2 (gate D1H cz120 CZ.b 15:50:33-38: the Combat Zone cage door 0002005F was OPEN - 'DOOR|skip
						// (notClosed)' - yet every leg into the cage went 'stage 0->2 why=door', doorBack, doorDead, blinkWait:
						// 3.7 s on one raider, the player down to 20 %; live 21:26 ArcJet the same with an open door): nothing
						// closed or locked on this blocked segment, and it is a door portal of the path (or a short gap right
						// ahead): an open doorway whose frame / leaf grazes the ray. He flies it on trust at door speed, centred,
						// for d2DoorTrust s; a body that stays blocked (navBodyBlocked) still escalates. 0 = D1H (the door AI).
						if (p.d2DoorTrust > 0 && !bodyBlocked && opened <= 0 && w.LockedDoorsSeen() == 0) {
							const bool portal = i < static_cast<int>(n.flags.size()) && n.flags[static_cast<size_t>(i)] != 0;
							bool       shortGap = Length(n.wp[i] - f.meChest) <= p.d2DoorTrustGap;
							if (!portal && !shortGap && CanCast(1)) {  // or what stops the ray is right at him (a frame / leaf he is in)
								double fr = -1;
								++_rays;
								if (w.Cast(f.meChest, n.wp[i], fr) && fr >= 0) {
									_st.d2LastGap = fr * Length(n.wp[i] - f.meChest);  // D2 tuner (log only)
									shortGap = _st.d2LastGap <= p.d2DoorTrustGap;
								}
							}
							if (portal || shortGap) {
								n.trustUntil = _now + p.d2DoorTrust;
								n.trustSeg = i;
								n.segReplanned = false;
								++_st.d2DoorTrusts;
								d2Trust = true;
							} else {
								++_st.d2TrustMiss;  // D2 tuner (log only): the blocker was beyond d2DoorTrustGap
							}
						}
					}
					// ROUND3 fix 2: at most navDoorTries door-AI tries per blocked spot; then this plan is dead and the chain
					// goes on (it looped NAV -> door AI -> NAV at the museum stairs for 10 s)
					if (!d2Trust) {
						const Vec3 at = n.floor[i];
						const bool same = n.doorHave && Length(Flat(at - n.doorAt)) < p.navDoorSame;
						if (same && n.doorTries >= (p.navFlyPush >= 0.5 ? 1 : static_cast<int>(p.navDoorTries))) {  // ROUND5: one push try per spot (doors that open were opened natively)
							n.dead = true;
							n.doorDead = true;
							++_st.doorDeads;
							return false;
						}
						if (!same) {
							n.doorHave = true;
							n.doorAt = at;
							n.doorTries = 0;
						}
						++n.doorTries;
						n.doorUntil = _now + p.navDoorFor;
					}
				}
			} else {
				n.segReplanned = false;
			}
		}
		if (_now < n.doorUntil) return false;  // ReachHidden runs the AI stage meanwhile (doors open for the AI)
		// remaining length + progress
		double remain = Length(n.wp[i] - f.meChest);
		for (int k = i; k < last; ++k) remain += Length(n.wp[k + 1] - n.wp[k]);
		n.remain = remain;
		// s4 fix lane 3 (soak 210131: 9.9 s pressed under a low ceiling, the leg "progressing" on every re-plan): a shorter
		// remain counts only when he has really moved navProgMove u since the last counted progress (a string pull or a
		// re-plan alone moves nobody). brain.navProgMove 0 = round 4.
		const bool moved = !(p.navProgMove > 0) || !n.progPosSet || Length(f.meChest - n.progPos) >= p.navProgMove - kTiny;
		if (remain < n.best - p.navProgStep && moved) {
			n.best = remain;
			n.progAt = _now;
			if (p.retNavProgress >= 0.5 && n.progPosSet && Length(f.meChest - n.progPos) >= std::max(1.0, p.navProgMove) - kTiny) n.movedAt = _now;
			n.progPos = f.meChest;
			n.progPosSet = true;
		} else if (_now - n.progAt >= p.navNoProg - kTiny) {
			// FINISH B2 fix lane 4 (brain.navDoorFirst; soaks GK1 / GK2, the ArcJet door 000DED7D): no progress with a closed door on this
			// segment - he stopped at it while the thin segment ray passed, so the blocked-segment escalation never got to OpenDoors - the door
			// opens now and the leg waits for it (no failed re-plan counted)
			if (p.navDoorFirst >= 0.5 && NavDoorFirst(f, p, w, i)) return hold();
			n.progAt = _now;
			n.replanNow = true;
			if (++n.fails >= static_cast<int>(p.navMaxFails)) {
				n.dead = true;
				return false;
			}
		}
		// corner speed
		const bool   tight = f.interiorCell || f.lowCeiling;
		const double vmax = tight ? p.navVMaxIndoor : p.navVMax;
		const Vec3   e = n.wp[i] - f.meChest;
		const double dist = Length(e);
		double       sp;
		if (i < last) {
			// the corner law (TurnSpeed), and never faster than an accel-limited turn that swings out at most navCornerSlack u
			// past the corner (a door is ~100-130 u wide: the TurnSpeed law alone ran wide at 57 degrees in RE-1)
			const double th = sp::nav::TurnDeg(f.meChest, n.wp[i], n.wp[i + 1]);
			double       vt = std::min(sp::nav::TurnSpeed(th, vmax, p.navV90, p.navVMin),
				std::sqrt(p.navAccel * p.navCornerSlack / std::max(1e-3, 1.0 - std::cos(th * sp::math::kDeg2Rad))));
			// ROUND4: a door portal (pinched to its middle by the planner) is passed at navDoorV at most, centred
			if (i < static_cast<int>(n.flags.size()) && n.flags[static_cast<size_t>(i)] != 0) vt = std::min(vt, p.navDoorV);
			sp = std::min(vmax, std::sqrt(vt * vt + 2.0 * std::clamp(p.navBrake, 0.05, 1.0) * p.navAccel * std::max(0.0, dist)));
		}
		else if (n.partial || (p.navOffGoal > 0 && c.id != kReturnNavId && Length(n.floor[static_cast<size_t>(last)] - c.feet) > p.navOffGoal))
			// ROUND3 fix 2: the door step of an island: arrive there (no approach speed at a wall); ROUND5 A2: and the last
			// floor point under / beside an off-navmesh foe (no overshoot past it)
			sp = std::min(vmax, std::max(p.navVMin, std::sqrt(2.0 * p.navBrake * p.navAccel * std::max(0.0, dist))));
		else sp = std::min(vmax, std::sqrt(p.approachMax * p.approachMax + 2.0 * p.navBrake * p.navAccel * std::max(0.0, dist - p.navReach)));
		if (n.trustSeg == i && _now < n.trustUntil + 0.5) sp = std::min(sp, std::max(p.navDoorV, p.navVMin));  // ROUND5 A2: through the door he opened
		out.move = Move::kNav;
		out.vDes = dist > 1e-3 ? e * (sp / dist) : Vec3{};
		out.aim = n.wp[i];
		out.aimVel = {};
		out.accel = p.navAccel;
		out.vMax = vmax;
		out.face = FlatDir(e);
		out.planKind = 4;  // Plan kinds 0-3 + 4 = nav (logs)
		out.navIdx = i;
		out.navN = static_cast<int>(n.wp.size());
		out.navRemain = remain;
		return true;
	}

	// FINISH B2 fix lane 4 (brain.navDoorFirst; the finB gate's K01, soaks GK1 20261027-011106 01:13:32.3-33.1 and GK2 013552
	// 01:38:38.2-38.8: the navmesh leg to the ArcJet turret 000DEDA4 stopped at (1049,-883) in front of the closed door 000DED7D, meV 5-52,
	// FB blockedFor 0.00-0.14; the leg's segment ray was CLEAR (the door leaf is thin), so the blocked-segment escalation - re-plan, then
	// OpenDoors on the second blocked check - never started (after every re-plan the clear ray reset segReplanned), and the no-progress
	// clock (navNoProg 0.6 s, navReplanGrace 0.3 s, navMaxFails 2) ended the leg 0.5-0.8 s later: '0->2 why=nav' -> push -> d2Wait ->
	// give-up 'unreach' -> the leash flip-flop with 000DED9B and 4 blinks instead of 2; the door opened 3.6 s / 50 s later. None of 20
	// earlier soaks (14 installed, 6 finA2) gave that turret up; the finA2 six and 10 installed opened the door 0.8-1.9 s after the pick). On a held leg (his body held or
	// no progress) a closed, unlocked, non-load door on the current segment - or on the next one when he is at its corner - is opened at
	// once; he hovers while it swings and the leg gets fresh no-progress time. At most one try per navDoorOpenGap s on a leg while none
	// opens (a door query each); one that opened stamps doorOpenAt as the old branch does. Pure brain logic: the world's OpenDoors is
	// the engine path the brain has always used (its own SEH shells, the load-door / lock checks inside).
	bool Brain::NavDoorFirst(const Facts& f, const Params& p, IWorld& w, int a_i)
	{
		NavState& n = _nav;
		if (!(p.navOpenDoors >= 0.5) || n.wp.empty() || a_i < 0 || a_i >= static_cast<int>(n.wp.size())) return false;
		if (_now - n.doorFirstAt < p.navDoorOpenGap - kTiny || _now - n.doorOpenAt < p.navDoorOpenGap - kTiny) return false;
		n.doorFirstAt = _now;
		// the segment ahead: to the current corner, or past it to the next one when he is at the corner (the door right behind it)
		Vec3 to = n.wp[static_cast<size_t>(a_i)];
		if (a_i + 1 < static_cast<int>(n.wp.size()) && Length(to - f.meChest) <= 2.0 * p.navReach + p.navDoorOpenR) to = n.wp[static_cast<size_t>(a_i) + 1];
		const int opened = w.OpenDoors(f.meChest, to, p.navDoorOpenR);
		if (opened <= 0) return false;
		_st.doorsOpened += static_cast<std::uint64_t>(opened);
		++_st.b2l4DoorFirst;
		n.doorOpenAt = _now;
		n.doorWaitUntil = _now + p.navDoorOpenWait;
		if (p.navDoorTrust > 0) n.trustUntil = n.doorWaitUntil + p.navDoorTrust;
		n.segChecked = -1;
		n.segReplanned = false;
		if (n.progAt < n.doorWaitUntil) {
			n.progAt = n.doorWaitUntil;  // fresh time: the swing, then navNoProg to pass it
			++_st.b2l4DoorFresh;
		}
		return true;
	}

	// R6 step 6 FIX: the "near" distance of the reach chain - a foe below him (chest lower than his) by its flat distance
	// (brain.nearFlatBelow 1), else the 3D distance
	static double NearD(const Facts& f, const Params& p, const Cand& c)
	{
		const Vec3   e = c.chest - f.meChest;
		const double d3 = Length(e);
		if (p.nearFlatBelow < 0.5 || !(e.z < 0)) return d3;
		const double df = Length(Flat(e));
		return std::isfinite(df) ? std::min(d3, df) : d3;
	}

	// s13 fix round 2 lane 2 (brain.throwBlinkFarR): another foe attacking the player (its combat target the player or a hit on it
	// within defendWindow: Cand::attackingPlayer), not kill-pending, within throwBlinkNearR of the player this frame
	bool Brain::ThrowBlinkNear(const Facts& f, const Params& p, std::uint32_t a_target) const
	{
		const auto& cs = f.cands;
		const int   n = std::min(static_cast<int>(cs.size()), static_cast<int>(_info.size()));
		for (int i = 0; i < n; ++i) {
			const Cand& o = cs[i];
			if (o.id == a_target || !o.attackingPlayer || o.killPending || !o.loaded || !o.sameSpace) continue;
			if (_info[i].dPlayer <= p.throwBlinkNearR + kTiny) return true;
		}
		return false;
	}

	void Brain::CommitReachBlink(std::uint32_t a_id, const Params& p, bool capDef, bool s2fExtDef, bool hurtNow, bool thrNow)
	{
		if (hurtNow) ++_st.d2DefendBlinks;  // D2 tuner
		if (thrNow) ++_st.czThrowBlinks;    // FINISH A2 fix lane 4 (brain.czThrowR)
		if (capDef) {                       // s12 fix r3 lane 3 (brain.defendCapBlink): one re-reach of this foe
			Mem& cm = M(a_id);
			if (_now - cm.reReachSince > p.defendReReachWin || _now < cm.reReachSince) {
				cm.reReachSince = _now;
				cm.reReaches = 0;
			}
			++cm.reReaches;
			++_capBlinkN;
		}
		if (s2fExtDef) ++_s2fDefBlinkN;     // step-2 fixer
		_lastBlinkAt = _now;
		D2NoteBlink();  // D2: the blink budget
		++_reachBlinks;
		++_st.reachBlinks;
		M(a_id).reachBlinkAt = _now;  // s12 r3 fix lane 1 (brain.blinkRepeatT)
	}

	// ROUND3 REACH: the stage chain for a hidden target (or a visible one whose straight segment is blocked).
	void Brain::ReachHidden(const Facts& f, const Params& p, const Ctx& cx, IWorld& w, Out& out, const Cand& c, const Info& in)
	{
		if (_stage < 0) {
			_triedNav = _triedRay = _doorAi = _aiTried = false;
			if (f.interiorCell) _triedRay = true;  // no sky indoors: skip the RAY stage
			// ROUND5 (in game round 4 R2, Concord deathclaw: seen 600 u away behind the museum corner, the sky stage put him
			// over it and he swung from above for 2.5 s, 18 swings, 1 hit): a SEEN target within handR whose straight line is
			// blocked starts on the navmesh (round the cover at its own floor level), not in the sky. brain.nearNavFirst 0: sky.
			// R6 step 6 FIX (gate s6, downtown at uncapped fps, 2 of 2 runs: 60 s hovering 13 m straight above the raiders,
			// 218-243 swings, 1-5 hits): the gravity-free capsule now holds the sky waypoint exactly (the old servo sagged ~250 u
			// under it, which put the foes back inside handR), so every foe below was > handR in 3D, every re-pick started on
			// the sky again and the sky stall (skyStallR, 3D) never fired. A foe BELOW him is measured by its flat distance:
			// seen within handR flat -> the navmesh first (down to its floor). brain.nearFlatBelow 0 = 3D.
			const bool nearSeen = p.nearNavFirst >= 0.5 && in.m && in.m->vis == 1 && NearD(f, p, c) <= p.handR;
			if (nearSeen) ++_st.nearNavFirsts;
			const bool skyFirst = !f.interiorCell && !f.lowCeiling && !nearSeen && !(in.m && TargetCovered(c, *in.m, p, w));
			_stage = skyFirst ? 1 : 0;
			_stageAt = _now;
			_growId = 0;      // explore cycle m1 fix lane 1 (brain.navGrowT): a new chain measures its own leg's best distance
			_chainAt = _now;  // ROUND3 fix 2: a visible frame leaves the chain only after a clear straight check since now
			_chainD0 = Length(c.chest - f.meChest);  // ROUND4 fix 2: the commit radius is measured from here too
			_stillT = 0;
			_stageWhy = skyFirst ? "sky" : "nav";
			// ROUND5 A2 (gate A1, c60-r4m-b museum upstairs: 6 re-picks in 16 s, each one "-1->0 why=nav" under the balcony,
			// 8 swings 0 hits, never killed): the chain already got to the push / blink on this foe within huntIgnore s (and
			// it stands where it stood): go on from there. brain.chainMemory 0 = round 5 A.
			// s3 fix lane 3 (gate s3 item 6, sc7 ghouls_in 20261014-071445: the near raider in a house was re-picked from 5103 /
			// 6064 u and the chain went straight to "-1->3 why=d2Wait" - a blind push from 5 km - then d2NoBlink and a give-up;
			// the same 3x on a far ghoul at 5168-5259 u: ~11 s of the 23 s): the push / blink stages are near-range stages, so a
			// remembered one resumes only while he is within chainMemNear u of the foe; farther, the chain starts over (the
			// navmesh leg carries him in, and the memory is written again near it). brain.chainMemNear 0 = any distance.
			const bool memNear = !(p.chainMemNear > 0) || Length(c.chest - f.meChest) <= p.chainMemNear;
			// FINISH B2 fix lane 4 (brain.noIdleNoResume; soaks GK1 01:13:39.414 / GK2 01:38:45.560: the ArcJet turret 000DEDA4, given up
			// 'unreach' a few s before, came back only as the no-idle fallback for the one frame the other turret was leashed; its chain memory
			// resumed the BLINK stage and he blinked in that frame - and the plain pick took the other turret back 4-7 ms later: the landing
			// wasted, a second blink to 000DEDA4 4-8 s later): a fallback pick of a foe that has not hit the player within d2DbHitWin s starts
			// at the navmesh (no resume at the BLINK stage); the push resume and every defend blink rule stay as they were
			const bool resumeOk = p.chainMemory >= 0.5 && in.m && in.m->chainStage >= 2 && _now - in.m->chainAt <= p.huntIgnore + kTiny &&
			                      Length(Flat(c.feet - in.m->chainFeet)) <= p.chainMemMove && memNear;
			const bool resumeUrgent = (c.attackingPlayer && (c.explosive || c.turned)) ||
			                          (c.suicider && (c.attackingPlayer || c.combatWithGroup || c.inCombat));
			const bool fbNoResume = !resumeUrgent && resumeOk && p.noIdleNoResume >= 0.5 && _noIdleSel && c.id == _target && in.m->chainStage >= 3 &&
			                        !(c.lastHitPlayerAt > -1e8 && _now >= c.lastHitPlayerAt - kTiny && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny);
			if (fbNoResume) ++_st.b2l4NoResume;
			if (resumeOk && !fbNoResume) {
				_triedNav = _triedRay = true;
				_aiTried = in.m->chainStage >= 3;
				_stage = in.m->chainStage;
				_stageWhy = "resume";
				++_st.chainResumes;
			}
			// completion lane 3 (brain.teleNoPress / teleShutHold, Tele 1): a hidden foe that does not hurt the player, behind a way
			// that was found shut (itself, or a shut foe within teleShutR seen from near here): no leg, no push - the chain ends now
			// (the pick loop gives it up: the next foe, or back to the player); it is chased again when the way's hold ends or it
			// hurts the player (the shield case)
			if (!(in.m && in.m->vis == 1) && TeleNoPressCase(c, f, p) && TeleShutNow(c, f, p)) {
				++_st.teleShutSkips;
				// no chain at all (it stays at -1: every frame asks again, so a foe that shows or hurts the player is taken at once;
				// no give-up churn): he guards the player's side toward it (the no-idle fallback keeps it as his target)
				_stage = -1;
				_stageWhy = "teleShut";
				out.reachStage = -1;
				out.stageWhy = "teleShut";
				if (!TeleGuardLeg(c, f, p, w, out)) {
					out.move = Move::kHold;
					out.vDes = {};
					out.aim = f.meChest;
					out.accel = p.flyAccel;
					out.vMax = 0;
				}
				return;
			}
		}
		// FINISH A2 fix lane 5 (brain.turnedGuardR; TurnedGuardBlink): a turned companion / ex-teammate near the player gets its defend
		// jump at once, whatever stage the chain is in; that jump is this frame's move (the chain itself is left out this frame - it
		// starts again at NAV after the jump, as after the BLINK stage's own). No jump (key 0, no landing, a gap): the chain as before.
		int atPlResumeStage = -1;
		double atPlResumeAt = 0, atPlAiBest = 0, atPlAiProgAt = 0, atPlStillT = 0;
		bool atPlTriedNav = false, atPlDoorAi = false;
		const char* atPlResumeWhy = nullptr;
		Out atPlRouteOut{};
		auto probeAtPlayer = [&]() {
			atPlResumeStage = _stage;
			atPlResumeAt = _stageAt;
			atPlResumeWhy = _stageWhy;
			atPlAiBest = _aiBest;
			atPlAiProgAt = _aiProgAt;
			atPlStillT = _stillT;
			atPlTriedNav = _triedNav;
			atPlDoorAi = _doorAi;
			atPlRouteOut = out;
			M(c.id).atPlHandAt = _now;
			++_st.atPlFarHands;
		};
		auto restoreAtPlayer = [&]() {
			if (atPlResumeStage < 0) return false;
			_stage = atPlResumeStage;
			_stageAt = atPlResumeAt;
			_stageWhy = atPlResumeWhy;
			_aiBest = atPlAiBest;
			_aiProgAt = atPlAiProgAt;
			_stillT = atPlStillT;
			_triedNav = atPlTriedNav;
			_doorAi = atPlDoorAi;
			out = atPlRouteOut;
			atPlResumeStage = -1;
			++_st.atPlRouteKeeps;
			return true;
		};
		bool flying = TurnedGuardBlink(f, p, w, out, c, in);
		for (int guard = flying ? 8 : 0; guard < 8; ++guard) {
			if (_stage == 0) {  // NAV
				flying = FollowNav(f, p, w, out, c);
				// completion lane 3 (brain.teleNavPress, Tele 1): the navmesh leg toward a foe that does not hurt the player held
				// pressed by the mover (FB) teleNavPress s - outside a door window - ends (the push stage next, then no blink: the
				// chain ends; Tele 0 had the reach blink at its end). In soak 131156 these legs pressed 2.8-4.6 s at ArcJet's way;
				// 0.9 s ends one before the FB stretch counts (>= 1 s)
				if (flying && p.teleNavPress > 0 && f.blockedFor >= p.teleNavPress - kTiny && _now >= _nav.doorUntil + 0.3 && TeleNoPressCase(c, f, p)) {
					++_st.teleNavPress;
					flying = false;
					NextStage("navPress");
					continue;
				}
				// explore cycle m1 fix lane 1 (brain.navGrowT; xp_m1r Saugus interior 09:52:54.1-59.1: the leg toward the turned soldier
				// FF002171 - 102 u from the player, shooting it 8 times - led AWAY, 1409 -> 2293 -> 3244 u straight, plan len 8070 u, and
				// only the push / blink stage 4.9 s later got him there): indoors, toward a foe that hit the player within d2DbHitWin s (the
				// D2 defend case: d2DefendBlink), a straight distance more than navGrowPad u above this leg's best for navGrowT s hands over
				// to the BLINK stage now - its defend blink may land in the player's view (hurtNow). Every BLINK rule (gap, budget exceptions,
				// a landing with a clear path, the repeat rule) stays; no landing = the stage's own push / cap as before
				if (flying && p.navGrowT > 0) {
					const bool hurtCase = !f.ext.on && f.interiorCell && p.d2DefendBlink >= 0.5 && c.attackingPlayer && c.lastHitPlayerAt > -1e8 &&
					                      _now >= c.lastHitPlayerAt - kTiny && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny;
					const double d = Length(c.chest - f.meChest);
					if (_growId != c.id) {
						_growId = c.id;
						_growBest = d;
						_growSince = -1;
					}
					_growBest = std::min(_growBest, d);
					if (hurtCase && d > _growBest + std::max(0.0, p.navGrowPad) + kTiny) {
						if (_growSince < 0) _growSince = _now;
						if (_now - _growSince >= p.navGrowT - kTiny) {
							++_st.navGrows;
							_growSince = -1;
							_triedNav = true;
							flying = false;
							StageTo(3, "navGrow");
							continue;
						}
					} else {
						_growSince = -1;
					}
				}
				// B2 fix lane 1 (brain.atPlFarR, AtPlayerFarHand): the foe at the player aiming at it, the long way round from him - the BLINK
				// stage now (its D2 defend blink before the blow lands; every BLINK rule stays: gap, repeat, a landing with a clear path line)
				if (flying && AtPlayerFarHand(c, f, p)) {
					probeAtPlayer();
					_triedNav = true;
					flying = false;
					StageTo(3, "atPlFar");
					continue;
				}
				if (flying) break;
				if (_now < _nav.doorUntil) {
					// ROUND3 fix 2: the door AI now has the AI stage's clocks (no progress navAiNoProg, at most navDoorTotal per
					// target) and every arming is logged (door / doorRe)
					const bool re = _nav.doorTries > 1;
					++(re ? _st.doorReArms : _st.doorArms);
					_doorAi = true;
					_stage = 2;
					_stageAt = _now;
					_aiBest = Length(c.chest - f.meChest);
					_aiProgAt = _now;
					_aiFrom = f.meFeet;
					_stageWhy = re ? "doorRe" : "door";
					continue;
				}
				if (_nav.dead) {
					if (_nav.island && p.navIslandAi >= 0.5) {
						// ROUND3 fix 2: at the door step of an island (a house behind a closed / cut door): the engine AI opens
						// doors; the sky path cannot enter a roofed spot
						_triedNav = true;
						if (f.interiorCell || (in.m && TargetCovered(c, *in.m, p, w))) _triedRay = true;
						StageTo(_triedRay ? 2 : 1, "island");
						continue;
					}
					// ROUND5 A2 (gate A1: the locked museum door, round 4 doorDead -> blink killed upstairs in 1.8-3.2 s): a locked
					// door ended the leg - the only route is shut, a push at it gets nowhere - so the blink goes first; he
					// pushes only while the blink waits (capped). brain.lockedBlink 0 = round 5 A (the push stage first).
					if (_nav.locked && p.lockedBlink >= 0.5) {
						_triedNav = true;
						_triedRay = true;
						_aiTried = true;
						++_st.lockedBlinks;
						StageTo(3, "locked");
						continue;
					}
					NextStage(_nav.offMesh ? "offMesh" : _nav.doorDead ? "doorDead" : "nav");
					continue;
				}
				break;  // plan rate-limited this frame: hold (navStill)
			}
			if (_stage == 1) {  // RAY (sky / side detour)
				// ROUND4 fix 2 (in game R1, Concord deathclaw: the sky approach hovered 226-383 u from it with vDes 70-290, no
				// progress for 2 s, then "give up unreach" and the ignore): near the target (skyStallR) and no progress for
				// skyStallT s in this stage -> the next stage (the navmesh path down to its floor, then the AI walk, then the
				// blink). skyStallT 0 = off.
				const double dSky = NearD(f, p, c);  // R6 step 6 FIX: flat for a foe below him (brain.nearFlatBelow)
				if (p.skyStallT > 0 && dSky <= p.skyStallR && _now - _progAt >= p.skyStallT - kTiny && _now - _stageAt >= p.skyStallT - kTiny) {
					++_st.skyStalls;
					NextStage("skyStall");
					continue;
				}
				flying = FollowPlan(f, p, cx, w, out, &c, c.feet, c.chest, c.height, false);
				if (!flying) {
					NextStage("rayFail");
					continue;
				}
				if (_exhaustedSince >= 0 && _now - _exhaustedSince >= p.navStageHold - kTiny) {
					flying = false;
					NextStage("rayEnd");
					continue;
				}
				break;
			}
			if (_stage == 2) {  // AI (engine path; the runtime raises his speed)
				// progress: closer by progressStep, or moved navDoorMove (the AI may walk round to a door or the stairs)
				const double d = Length(c.chest - f.meChest);
				// ROUND4 (in game R3, Sanctuary house: at the doorway (147 u) the door AI walked him away to 337 u and "moved
				// 120 u" counted as progress): the door AI counts only getting closer; the plain AI stage keeps both
				// ROUND4 fix 2: ... and so does the AI walk after a locked door ended the navmesh leg
				const bool moveCounts = !((_doorAi || (_nav.forId == c.id && _nav.locked)) && p.doorAiDist >= 0.5);
				if (d < _aiBest - p.progressStep || (moveCounts && Length(Flat(f.meFeet - _aiFrom)) >= p.navDoorMove)) {
					_aiBest = std::min(_aiBest, d);
					_aiProgAt = _now;
					_aiFrom = f.meFeet;
				}
				// ROUND5 A2 (gate A1, c120-r5: "0->2 why=nav", the push slid back and forth, then 6 s of blinkWait pushing at d
				// 1287-1795 with noProg 10.5 s): the flown push slides about by design, so "moved navDoorMove" is no progress for
				// it; its cap reads the push's real progress clock (closer to the goal by progressStep, or a corner passed) -
				// navOffPush s for a foe off the navmesh (straight at it: it gets there quickly or not at all), navPushNoProg s
				// otherwise. The engine-AI law (navFlyPush 0) keeps its clock.
				const bool   pushing = p.navFlyPush >= 0.5;
				const double pushLim = (_nav.forId == c.id && _nav.offMesh) ? p.navOffPush : p.navPushNoProg;
				const bool   noProg = (pushing && pushLim > 0) ? _now - std::max(_stageAt, _pushRealProgAt) >= pushLim - kTiny : _now - _aiProgAt >= p.navAiNoProg - kTiny;
				// D2 tuner (goal 3): toward a foe shooting the player, d2CageSkip s of push (or door push) without real progress hands
				// over to the blink stage (the defend blink)
				const bool   cageSkip = p.d2CageSkip > 0 && p.d2DefendBlink >= 0.5 && f.interiorCell && c.attackingPlayer &&
				                        (_now - c.lastHitPlayerAt <= p.d2DbHitWin || cx.health < p.d2CageHp) && _now - std::max(_stageAt, _pushRealProgAt) >= p.d2CageSkip - kTiny;
				if (noProg && pushing) ++_st.pushCaps;  // (frames)
				// FINISH A2 fix lane 2 r2 (brain.pushPressT; gate finA K03: the ArcJet soak pushes at turrets on the platform above -
				// '0->2 why=offMesh / nav', the mover holding him pressed up under the upper floor, HOTFIX headroom 0-14, FB blockedFor
				// 1.5-3.4 s, BRAIN noProg 1.2-2.6 s): indoors, a flown push the mover holds pressed pushPressT s while it makes no real
				// progress for as long has failed now - the BLINK stage next (its rules decide: a blink, the D2 wait / last resort, the
				// end), not at navPushNoProg. Not the door AI (a door may still be swinging open).
				const bool pressFail = pushing && p.pushPressT > 0 && f.interiorCell && f.blockedFor >= p.pushPressT - kTiny &&
				                       _now - std::max(_stageAt, _pushRealProgAt) >= p.pushPressT - kTiny;
				if (_doorAi) {
					_doorT += _dt;
					// ROUND3 fix 2 (in game R2: the door AI never timed out, so BLINK was never reached; museum 10 s, stairs):
					// no progress for navAiNoProg, or navDoorTotal s of door AI on this target -> the door is not passable
					// for him: the navmesh is done and the AI was this stage, so on to the sky path (open target) or BLINK
					// ROUND5: the flown push at a door that did not open is capped like the plain push stage (navAiMax)
					const bool pushCap = p.navFlyPush >= 0.5 && _now - _stageAt >= p.navAiMax - kTiny;
					if (noProg || pushCap || _doorT >= p.navDoorTotal - kTiny || cageSkip) {
						_doorAi = false;
						_nav.dead = true;
						_nav.doorDead = true;
						_triedNav = true;
						_aiTried = true;  // the door AI was the AI stage (no second AI walk after the sky path)
						++_st.doorFails;
						const bool skyOk = !_triedRay && !f.interiorCell && !(in.m && TargetCovered(c, *in.m, p, w));
						StageTo(skyOk ? 1 : 3, noProg ? "doorNoProg" : "doorMax");
						continue;
					}
					if (_now >= _nav.doorUntil) {  // the window is over and he got somewhere: the navmesh leg again
						_doorAi = false;
						_stage = 0;
						_stageAt = _now;
						_nav.segChecked = -1;
						_stageWhy = "doorBack";
						continue;
					}
				} else if (_now - _stageAt >= p.navAiMax - kTiny || noProg) {
					_aiTried = true;
					NextStage(!pushing ? "ai" : noProg ? "pushNoProg" : "pushMax");
					continue;
				} else if (pressFail) {  // FINISH A2 fix lane 2 r2 (brain.pushPressT)
					_aiTried = true;
					++_st.pushPressCuts;
					NextStage("pushPress");
					continue;
				} else if (cageSkip) {
					// D2 tuner (goal 3, d2b-cz120: cage reach 2.4-3.4 s = navmesh leg ~1 s + this push ~0.6 s + blink): a foe that
					// is shooting the player gets the blink stage after d2CageSkip s of push without real progress
					_aiTried = true;
					NextStage("d2Cage");
					continue;
				}
				// ROUND5: he flies it himself (the engine AI never moved him in game); brain.navFlyPush 0 = the AI walk
				if (p.navFlyPush >= 0.5) {
					FlyPush(f, p, w, out, c.chest, c.id);
				} else {
					AiChase(f, p, out);
					out.chaseBoost = true;
				}
				flying = true;  // the engine moves him
				// Snapshot the actual stage output (FlyPush or engine AI), after its ordinary progress/cap checks.
				// A failed shortcut must not replace an existing native push with a fabricated zero-speed AI chase.
				if (AtPlayerFarHand(c, f, p)) {
					probeAtPlayer();
					_doorAi = false;
					_triedNav = true;
					StageTo(3, "atPlFar");
					continue;
				}
				break;
			}
			if (_stage == 3) {  // BLINK (only where the player sees neither end)
				// R6 step 10 ([R6] Tele 1, ARCH §4.11 / §3.9.5): no reach blink. An attacker of the player that the chain could not
				// reach is SHIELDED here (he hovers between it and the player) until it can be reached, another foe that fights the
				// group can be taken (then this one is given up: the pick goes to that one), or the Pilot permits the defend jump.
				// Anyone else: the old refusal below (he pushes at it 2 x navPushNoProg s, then the chain ends: the next foe).
				if (TeleOn(f) && TeleCase(c, f)) {
					if (TeleOther(c, f)) {
						if (_teleShieldId == c.id) ++_st.teleShieldOther;
						_teleShieldId = 0;
						NextStage("shieldOther");
						continue;
					}
					if (TeleShield(c, f, p, w, out)) {
						flying = true;
						break;
					}
					if (_stage != 3) continue;  // tele.shieldRetry: the fresh reach chain starts at NAV now
				}
				if (_teleShieldId == c.id) _teleShieldId = 0;
				const bool   teleNo = TeleOn(f);  // R6 step 10: every other blink of this stage is refused (like a D2 rule)
				if (teleNo) ++_st.teleBlinkRefused;  // (frames)
				const double d = Length(c.chest - f.meChest);
				// ROUND3 fix 2: navBlinkMin keeps him flying at near foes; one the chain found unreachable (an island, a door
				// he could not pass) is blinked to from nearer too (sealed room right behind a wall)
				// ROUND4: ... or NAV and the AI walk both failed on it (the museum balcony: a foe 230 u below him, never within
				// navBlinkMin, so the chain ended in a give-up loop)
				const bool   unreachable = (_nav.forId == c.id && (_nav.island || _nav.doorDead || _nav.offMesh)) || (_triedNav && _aiTried);
				// (ROUND5 A2: an off-navmesh foe the push could not get at is blinked to from any distance - he hovered 109-160 u
				// from the museum raider on the other side of the balcony floor)
				const bool   offM = _nav.forId == c.id && _nav.offMesh;
				// D2 tuner (GATE_D1H1 fail 2, d1h-cz120-c 17:04:36-39: 'blinkWait' at d 160-370 for 3 s on the cage raider FF000FB1
				// - the blink waited for a moment the player does not see, which never comes in the arena - while FB1 / FB4 / FB6
				// shot the player to 20 %): toward a foe that hit the player within d2DbHitWin s (or any attacker of the player
				// while the player is below d2CageHp) the blink may land in view, from navBlinkNear, d2DefendBlinkGap s apart
				// D2 tuner (d2b-hf120 CZ.b: the blink budget spent, the cage raider FF00112F was pushed at (d2Wait), given up
				// 'unreach' and held by the no-idle fallback - he stood 25.7 s): d2LastResort 1: a foe fighting the group (indoors)
				// that the D2 rules (budget / seen) kept him from blinking to gets ONE blink when the push has failed, in view
				// if need be, instead of the give-up
				// FINISH A2 fix lane 2 r2 (brain.pushPressT): the D2-refused push the mover holds pressed pushPressT s (and kPressWaitMin s
				// of this stage) without real progress has failed as surely as one that ran 2 x navPushNoProg s: the last resort is due now
				// (the soak's in2 wave raiders on the Speakeasy shelves, 'd2Wait' 1.6-3.4 s pressed before the same blink)
				const double pressT = _now - std::max(_stageAt, _pushRealProgAt);
				const bool   d2Pressed = p.pushPressT > 0 && f.interiorCell && f.blockedFor >= p.pushPressT - kTiny &&
				                       pressT >= std::min(p.pushPressT, kPressWaitMin) - kTiny;
				// finB (finA2 REVIEW 3, gate K01 blinks 10.6 vs 7.0 / 10 min): the early pressed last resort (pushPressT) only toward an
				// attacker of the player; a foe fighting only the group keeps finA's 2 x navPushNoProg (fewer in-view blinks)
				const bool   lastResort = p.d2LastResort >= 0.5 && f.interiorCell && (c.attackingPlayer || c.combatWithGroup) && std::string_view(_stageWhy) == "d2Wait" &&
				                        (pressT >= 2.0 * std::max(0.25, p.navPushNoProg) - kTiny || (d2Pressed && (c.attackingPlayer || p.pushPressAtk < 0.5)));
				if (lastResort && d2Pressed && pressT < 2.0 * std::max(0.25, p.navPushNoProg) - kTiny && _d2PressLastId != c.id) {
					_d2PressLastId = c.id;
					++_st.d2PressLast;
				}
				// step-2 fixer (s2fDefendBlink): outdoors too, toward a foe that hit the player within d2DbHitWin s while the player
				// is below s2fDefendHp (the exterior ban and the unseen-landing rule do not hold it back then)
				const double hpS2f = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
				// R6 step 4 (Facts::ext.on): the D2 defend / last-resort blinks are replaced by the Warden's permitted defend jump
				// (DefendJump, why 5); this stage blinks only with tele.oldReachBlinks 1, by the D2 rules, both ends out of view
				const bool   extOn = f.ext.on;
				const bool   s2fExtDef = !extOn && p.s2fDefendBlink >= 0.5 && c.attackingPlayer && _now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny && hpS2f < p.s2fDefendHp;
				// s7.5 fix lane 3 (brain.defendReReach; run 123733: blinkCap on the CZ turret, every landing in the player's view):
				// an attacker of the player whose chain already ended once (DefendReReach) gets the D2 defend blink indoors - in
				// view if need be, from navBlinkNear, outside the budget - on its next pass
				const bool   reReachLR = p.defendReReach >= 0.5 && f.interiorCell && c.attackingPlayer && c.id == _reReachId && _now < _reReachUntil;
				// s12 fix r3 lane 3 (brain.defendCapBlink; R2 hf 100859 CZ.b 10:12:39.58-41.69: 'blinkWait' 0.85 s - every landing
				// in the player's view in the arena -, 'blinkCap', 'give up FF00111C unreach', the re-reach restarted at nav / door and
				// blinked in view only at 41.69, while FF00111C hit the player at 40.53, 41.17 and 42.17, 1.00 -> 0.30): a blink wait
				// that has run defendCapWait s gives the re-reach's in-view landing now instead of the cap, the give-up and the restart,
				// on the same conditions as DefendReReach (an attacker of the player indoors, no other attacker of the player pickable,
				// within defendReReachMax; it counts as one) and within defendCapBlinkR of the player. Without a landing even then: the
				// wait goes on and ends as before.
				// (also a blink the D2 rules refused - 'd2Wait': his take-off in view, the budget - which the re-reach lifts too; the wait
				// counts from the stage start or his last real progress: defendCapWait s, at most what the chain itself waits: the in-game
				// blinkWait ran 0.85 s, the desktop cage's d2Wait 1.5 s until the seen no-progress rule ended it)
				const std::string_view capWhy(_stageWhy ? _stageWhy : "");
				const double           capT = _now - std::max(_stageAt, _pushRealProgAt);
				const bool             capDue = (capWhy == "blinkWait" || capWhy == "d2Wait") && p.defendCapWait > 0 && capT >= p.defendCapWait - kTiny;
				const bool   capDef = !extOn && !reReachLR && p.defendCapBlink >= 0.5 && f.interiorCell && c.attackingPlayer && in.dPlayer <= p.defendCapBlinkR + kTiny && capDue &&
				                    CapDefendOk(c, f, p);
				const bool   hurtOld = !extOn && ((p.d2DefendBlink >= 0.5 && f.interiorCell && c.attackingPlayer &&
				                        (_now - c.lastHitPlayerAt <= p.d2DbHitWin || cx.health < p.d2CageHp)) || lastResort || s2fExtDef || reReachLR || capDef);
				// FINISH A2 fix lane 4 (brain.czThrowR, CzThrowCase): a thrower aiming at the player indoors is hurting it NOW - the D2 defend
				// blink (in view, outside the budget, from navBlinkNear) before its first throw lands, not after
				const bool   thrNow = !hurtOld && !extOn && CzThrowCase(c, f, p);
				// B2 fix lane 1 (brain.atPlFarR, AtPlayerFar): a foe at the player aiming at it, the long way round from him (another floor / a
				// long navmesh way), is hurting it NOW too - its blow is coming. (A foe near him keeps the D2 rules: no extra blink near the player)
				const bool   atPlNow = !hurtOld && !thrNow && !extOn && AtPlayerFar(c, f, p);
				const bool   hurtNow = hurtOld || thrNow || atPlNow;
				if (thrNow) ++_st.czThrowFrames;
				if (atPlNow) ++_st.atPlFarFrames;
				const double dMin = offM ? 0.0 : (unreachable || hurtNow) ? p.navBlinkNear : p.navBlinkMin;
				// DPREP-FOLLOW (GATE_B fail 2, c60-r5-b 00:05:40.93 'stage 3->4 why=blink d=108' then 'HUNT|return', 3.5 s idle,
				// a blink back: 12.2 s): the blink stage reached while he is already nearer than a blink may jump. With
				// brain.nearBlinkPush he pushes at it first (below); after navPushNoProg s without real progress the blink may
				// jump from this near too - only to a landing with a clear path line to the foe (brain.blinkNeedPath, and
				// BlinkSpot's own path ray from the foe) - instead of the give-up.
				const bool   tooNear = p.navBlink >= 0.5 && d < dMin;
				const bool   nearLate = tooNear && p.nearBlinkPush >= 0.5 && _now - std::max(_stageAt, _pushRealProgAt) >= p.navPushNoProg - kTiny;
				// explore m1 fix lane 3 (brain.closeDefendR): the close-defend stage's blink goes now at any distance (he is held at what is
				// between - pressed at the balcony 110 u from the gunner - not at it); its landing still needs a clear path line to the foe
				const bool   cdNow = hurtNow && c.id == _cdId && _now >= _cdAt - kTiny && _now - _cdAt < 1.0 - kTiny;
				const bool   can = p.navBlink >= 0.5 && _reachBlinks < static_cast<int>(p.navBlinksPer) && (d >= dMin || nearLate || cdNow);
				// DPREP-FOLLOW: a landing must let him hit: a clear PATH line (railings / glass stop it, unlike the sight ray)
				// from the landing chest to just short of the foe's chest. brain.blinkNeedPath 0 = round 5 B (sight only).
				auto pathOk = [&](const Vec3& a_chest) {
					if (p.blinkNeedPath < 0.5) return true;
					if (!CanCast(1)) return false;
					const Vec3 to = c.chest - Normalize(c.chest - a_chest, { 1, 0, 0 }) * std::min(30.0, 0.5 * Length(c.chest - a_chest));
					if (RClear(w, a_chest, to)) return true;
					++_st.blinkPathRejects;
					return false;
				};
				// ROUND4 (in game R3, museum upstairs: blinkWait until the 10 s cap in every run, because the player in the same
				// hall always saw HIM): the chain got here because the path is blocked, so a short blink (<= navBlinkSeenMax u)
				// to a spot the player cannot see is allowed even while he is seen. Rollback: brain.navBlinkSeen = 0.
				const bool   gapOk = _now - _lastBlinkAt >= (hurtNow ? std::min(p.navBlinkGap, p.d2DefendBlinkGap) : p.navBlinkGap) - kTiny;
				// D2: no blink in an exterior cell (d2BlinkExterior 0), none with a seen take-off (d2BlinkSeen 0), none beyond the
				// budget (d2BlinkBudget per d2BlinkWindow s). Survival first: a seen take-off / a blink beyond the budget toward a
				// foe attacking the player while the player is below d2BlinkSeenDefendHp of health stays allowed
				const bool   defendSeen = (p.d2BlinkSeenDefendHp > 0 && c.attackingPlayer && cx.health < p.d2BlinkSeenDefendHp) || hurtNow;
				const bool   budgetNo = !defendSeen && p.d2BlinkBudget > 0 && D2BlinksInWindow(p) >= static_cast<int>(p.d2BlinkBudget);
				const bool   extNoBlink = extOn && !ExtBlinkOk(c, f);  // R6 step 4: refused like a D2 rule (he pushes at it, then the chain ends)
				if (extNoBlink && can && gapOk) ++_st.extBlinkRefused;  // (tries)
				// R6 step 9 ([R6] Route 1, ARCH 4.11): only a D2 DEFEND blink may fire from this stage (no plain reach blink)
				const bool   routeNoBlink = f.route.on && f.route.noReachBlinks && !(hurtNow && c.attackingPlayer);  // s9 integrator: the last-resort blink toward a foe that only fights the group is a REACH blink (in game sc12 06:33: 4 of them)
				if (routeNoBlink && can && gapOk) ++_st.routeBlinkRefused;  // (tries)
				// s12 r3 fix lane 1 (brain.blinkRepeatT; K01, the s12r2 candidate's 7 soaks 9.45 reach blinks / 10 min vs the installed 5.95:
				// 6 repeat blinks on one foe within 6 s and 6 landings that fell 570-840 u at once - the Concord Speakeasy raiders on the
				// upper floor, 122205 12:29:25.6 / 26.7 FF001608 twice in 1.1 s, 0 kills - the installed 8 soaks: 1 repeat, 2 falls): a
				// second reach blink to the same foe within blinkRepeatT s of the last is refused like a D2 rule (he pushes at it, then
				// the chain ends: the next foe) unless the player is below lowHealth (survival first). The first blink is unchanged.
				Mem&         rbm = M(c.id);
				const double hpR = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
				const bool   repeatWin = p.blinkRepeatT > 0 && _now >= rbm.reachBlinkAt - kTiny && _now - rbm.reachBlinkAt < p.blinkRepeatT - kTiny && !(hpR < p.lowHealth);
				// completion side lane 4 s13r2 (brain.blinkRepeatDefend; REVIEW_r3 1): the repeat rule also refused the D2 DEFEND blink
				// (hurtNow) at a raider shooting the player from a room or an upper floor while the player was above lowHealth. The K01
				// repeats it was made for were landings that fell straight back out with no hit (the Speakeasy ledge, 0 kills). Now a
				// defend blink toward a foe attacking the player is refused as a repeat only when his last blink at it gave no hit on it
				// within blinkRepeatOkT s; after a blink that hit it, the next defend blink goes at once. Plain reach blinks: as before.
				const bool   lastBlinkHit = rbm.hitByMeAt >= rbm.reachBlinkAt - kTiny && rbm.hitByMeAt - rbm.reachBlinkAt <= p.blinkRepeatOkT + kTiny;
				const bool   repeatDefOk = repeatWin && p.blinkRepeatDefend >= 0.5 && hurtNow && c.attackingPlayer && lastBlinkHit;
				const bool   repeatNo = repeatWin && !repeatDefOk;
				if (repeatNo && can && gapOk) ++_st.blinkRepeatNo;  // (tries)
				if (repeatDefOk && can && gapOk) ++_repeatDefOkN;   // (tries)
				// s13 fix round 2 lane 2 (brain.throwBlinkFarR; K01, the s13r1 gate soaks: 1-5 of the 9-14 reach blinks a soak went to
				// throwers - prot:keepBomber picks, hidden, 1100-2700 u out - while raiders hit the player up close, GK2 12:23:34-38): no
				// reach blink to a hidden thrower beyond throwBlinkFarR of the player while another foe attacking the player is within
				// throwBlinkNearR of it. The chain ends at once (the next pick is the foe at the player) - no blink, and no d2Wait push
				// away from the player either.
				if (p.throwBlinkFarR > 0 && c.explosive && !(in.m && in.m->vis == 1) && in.dPlayer > p.throwBlinkFarR + kTiny && can && gapOk && ThrowBlinkNear(f, p, c.id)) {
					++_st.throwBlinkNo;
					NextStage("throwFar");
					continue;
				}
				// s13 fix round 3 lane 4 (brain.blinkIdleNo; K01): no reach blink to a foe that fights nobody of the group - it neither
				// attacks the player nor has the player / him / a teammate as its combat target, nor hit any of them within hitMemory s
				// (tier P / FP: the ArcJet robots behind a shut way the soaks blinked to). The chain ends at once (no d2Wait push either).
				const bool fightsGroup = c.attackingPlayer || c.combatWithGroup || c.combatWithPlayer ||
				                         (c.lastHitGroupAt > -1e8 && _now >= c.lastHitGroupAt && _now - c.lastHitGroupAt <= p.hitMemory + kTiny);
				if (p.blinkIdleNo >= 0.5 && !fightsGroup && !hurtNow && can && gapOk) {
					++_st.blinkIdleNo;
					NextStage("idleNoBlink");
					continue;
				}
				// cx2: filter actual landing candidates, not an ID or a 3D sphere around a foe's old floor.
				// Current/recent attackers of the player retain every old defend opportunity even at the same spot.
				const Vec3* refusedLanding = RefusedLandingFor(c, p);
				const bool   extNo = (p.d2BlinkExterior < 0.5 && !f.interiorCell && !s2fExtDef) || budgetNo || extNoBlink || routeNoBlink || teleNo || repeatNo;
				const bool   seenMe = can && gapOk && !extNo && PlayerSees(f, p, w, f.meChest);
				const bool   seenOk = !extOn && p.navBlinkSeen >= 0.5 && (p.d2BlinkSeen >= 0.5 || defendSeen) && d <= p.navBlinkSeenMax;
				if (seenMe && defendSeen && p.d2BlinkSeen < 0.5 && can && gapOk && !extNo) ++_st.d2SeenDefendBlinks;  // (tries)
				const bool   allowed = can && gapOk && !extNo && (!seenMe || seenOk);
				if (!allowed && restoreAtPlayer()) { flying = true; break; }
				// completion lane 3 (brain.teleNoPress / teleWaitPress, Tele 1): the reach blink is refused (no teleports) and this foe
				// does not hurt the player. The step-10 answer was 'd2Wait' - 2 x navPushNoProg s of push straight at it, into the wall
				// (30 of the 59 Tele-1 soak stretches) - then 'd2NoBlink'. Now: when the push stage already failed on it, or the push
				// here is pressed (FB) teleWaitPress s, there is no physical way: the chain ends at once and the way is marked shut.
				if (teleNo && TeleNoPressCase(c, f, p) && (_aiTried || f.blockedFor >= p.teleWaitPress - kTiny)) {
					++_st.teleNoWay;
					TeleShutMark(c, f, p);
					_stage = 4;
					_stageAt = _now;
					_stageWhy = "teleNoWay";
					++_st.stageAdv;
					continue;
				}
				// D2: refused by a D2 rule -> he keeps pushing at the foe (the TABS answer: fly at it) for 2 x navPushNoProg s
				// without real progress, then the chain ends (give-up, the next foe at once) - never a long blink wait
				if ((extNo || (seenMe && !seenOk && p.d2BlinkSeen < 0.5)) && can && p.navBlinkWait >= 0.5) {
					if (std::string_view(_stageWhy) != "d2Wait") {
						++(budgetNo ? _st.d2BlinkBudgetNo : extNo ? _st.d2BlinkExt : _st.d2BlinkSeen);
						_stageWhy = "d2Wait";
					}
					if (_now - std::max(_stageAt, _pushRealProgAt) >= 2.0 * std::max(0.25, p.navPushNoProg) - kTiny) {
						NextStage("d2NoBlink");
						continue;
					}
					// FINISH A2 fix lane 2 r2 (brain.pushPressT): pressed without progress - toward a foe that fights nobody of the group
					// (no last resort for it) the chain ends now, not after 2 x navPushNoProg s of pressing into the floor above (gate
					// finA K03: 16 of the 6 soaks' 97 stuck episodes were these pushes, 1.15-4.4 s each)
					if (d2Pressed && !(c.attackingPlayer || c.combatWithGroup)) {
						++_st.d2PressEnds;
						NextStage("d2Press");
						continue;
					}
					if (p.navFlyPush >= 0.5) {
						FlyPush(f, p, w, out, c.chest, c.id);
					} else {
						AiChase(f, p, out);
						out.chaseBoost = true;
					}
					flying = true;
					break;
				}
				if (allowed && seenMe) ++_st.seenBlinks;  // (tries while seen)
				Vec3 spot{};
				bool have = false, rejectedLanding = false;
				if (allowed) _rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
				if (allowed && _nav.forId == c.id) {
					for (int k = static_cast<int>(_nav.floor.size()) - 1; k >= 0 && !have && CanCast(2); --k) {
						const double dd = Length(Flat(_nav.floor[k] - c.feet));
						if (dd < p.navBlinkNear || dd > p.navBlinkFar) continue;
						if (RefusedBlinkLanding(_nav.floor[k], refusedLanding, p.blinkUndoneR)) {
							rejectedLanding = true;
							continue;
						}
						const Vec3 chest = _nav.floor[k] + Vec3{ 0, 0, f.meChest.z - f.meFeet.z };
						if (RClearTo(w, chest, c.chest, c.id, p.sightReach) && (hurtNow || !PlayerSees(f, p, w, chest)) && pathOk(chest)) {  // D2 tuner: in view when hurtNow
							spot = _nav.floor[k];
							have = true;
						}
					}
				}
				if (allowed && !have) {
					int r = 0;
					have = BlinkSpot(c, f.meFeet, p, w, spot, r, std::max(0, _rayCap - _rays), refusedLanding, p.blinkUndoneR, &rejectedLanding);
					_rays += r;
					if (have && !hurtNow) have = !PlayerSees(f, p, w, spot + Vec3{ 0, 0, f.meChest.z - f.meFeet.z });  // D2 tuner: in view when hurtNow
					if (have) have = pathOk(spot + Vec3{ 0, 0, f.meChest.z - f.meFeet.z });
				}
				if (!have && restoreAtPlayer()) { flying = true; break; }
				if (allowed && !have && rejectedLanding) {
					M(c.id).undoneRefusalAt = _now;
					++_st.blinkUndoneNo;
					NextStage("undone");
					continue;
				}
				if (have) {
					if (atPlResumeStage >= 0) {
						auto& r = _atPlRecovery;
						r.nav = _nav; r.target = c.id; r.pending = true; r.requestedAt = _now;
						r.stage = atPlResumeStage; r.stageAt = atPlResumeAt; r.why = atPlResumeWhy;
						r.aiBest = atPlAiBest; r.aiProgAt = atPlAiProgAt; r.stillT = atPlStillT;
						r.triedNav = atPlTriedNav; r.doorAi = atPlDoorAi;
						r.capDef = capDef; r.s2fExtDef = s2fExtDef; r.hurtNow = hurtNow; r.thrNow = thrNow;
					}
					out.blink = true;
					out.blinkFeet = spot;
					out.blinkWhy = 3;
					_blinkLand = spot;  // FINISH B2 Fable (brain.blinkUndoneT): the landing, for OnTeleport's undone check
					_blinkLandId = c.id;
					_blinkLandAt = _now;
					_blinkLandConfirmed = false;
					out.blinkDefend = hurtNow && c.attackingPlayer;  // R6 step 9 (s9 integrator: only toward an attacker of the player)
					if (atPlResumeStage < 0) CommitReachBlink(c.id, p, capDef, s2fExtDef, hurtNow, thrNow);
					_nav = NavState{};  // fresh plan after the jump (usually in sight now)
					_triedNav = _doorAi = false;
					_stage = 0;
					_stageAt = _now;
					_stageWhy = "blink";
					out.move = Move::kHold;
					out.vDes = {};
					out.aim = f.meChest;
					out.accel = p.flyAccel;
					out.vMax = 0;
					flying = true;  // the blink is this frame's move
					break;
				}
				// ROUND3 fix 3 (in game R1, museum: "stage 2->4 why=blink" then the target dropped for 2.5 s, 4.0 s still): a
				// blink that is possible but not now (the player looks, the gap, no unseen spot yet) keeps the target; the
				// engine AI chase goes on meanwhile, and the hidden cap (navHiddenMax) still ends it. Rollback:
				// brain.navBlinkWait = 0 (give up at once).
				// ROUND5 A2 (gate A1, c120-r5: 6 s of blinkWait pushing, no progress): the push while the blink waits ends like
				// the push stage (no real progress for navPushNoProg s) when the blink may happen now but finds no spot; a wait
				// for the blink gap or for the player to look away (navBlinkSeen 0, or beyond navBlinkSeenMax) keeps it going
				// (the hidden caps still end it)
				const bool waitCap = p.navFlyPush >= 0.5 && p.navPushNoProg > 0 && allowed && !have &&
				                     _now - std::max(_stageAt, _pushRealProgAt) >= p.navPushNoProg - kTiny;
				if (waitCap && can && p.navBlinkWait >= 0.5) {
					++_st.pushCaps;
					NextStage("blinkCap");
					continue;
				}
				// FINISH A2 fix lane 2 r2 (brain.pushPressT): a blink wait the mover holds pressed toward a foe that fights nobody of the
				// group ends the chain now (its blink would be a reach blink to a foe that hurts nobody; the wait pressed into the
				// floor above). Toward a foe fighting the group the wait goes on: its blink comes at the gap / the unseen moment.
				if (can && p.navBlinkWait >= 0.5 && d2Pressed && !(c.attackingPlayer || c.combatWithGroup || hurtNow)) {
					++_st.waitPressEnds;
					NextStage("waitPress");
					continue;
				}
				if (can && p.navBlinkWait >= 0.5) {
					if (p.navFlyPush >= 0.5) {  // ROUND5: keep flying at it (the AI walk stood still)
						FlyPush(f, p, w, out, c.chest, c.id);
					} else {
						AiChase(f, p, out);
						out.chaseBoost = true;
					}
					if (std::string_view(_stageWhy) != "blinkWait") ++_st.blinkWaits;
					_stageWhy = "blinkWait";
					flying = true;
					break;
				}
				// DPREP-FOLLOW: too near to blink is no reason to give up: he is right at it (TABS keeps dashing at a foe it
				// is touching). The flown push at its chest (side steps along what is between, its own progress clocks);
				// the seen no-progress rule and the hidden caps still end it. brain.nearBlinkPush 0 = round 5 B (give up).
				if (tooNear && p.nearBlinkPush >= 0.5) {
					FlyPush(f, p, w, out, c.chest, c.id);
					if (std::string_view(_stageWhy) != "nearPush") _stageWhy = "nearPush";
					++_st.nearBlinkPushes;  // frames
					flying = true;
					break;
				}
				NextStage("blink");
				continue;
			}
			break;  // stage 4: the pick loop gives up
		}
		if (p.chainMemory >= 0.5) NoteChain(c.id, c.feet);  // ROUND5 A2: the push / blink stage is kept on this foe
		out.reachStage = _stage;
		out.stageWhy = _stageWhy;
		// completion lane 3 (brain.teleNoPress, Tele 1): the chain ended with no way to this foe; while it stays his target (the
		// no-idle fallback keeps an attacker of the player) he guards the player's side toward it instead of standing at the wall
		if (!flying && _stage >= 4 && _stageWhy && (std::string_view(_stageWhy) == "teleShut" || std::string_view(_stageWhy) == "teleNoWay") &&
			TeleNoPressCase(c, f, p) && TeleGuardLeg(c, f, p, w, out))
			flying = true;
		if (!flying && out.move != Move::kAiChase) {  // never still longer than navStill
			out.move = Move::kHold;
			out.vDes = {};
			out.aim = f.meChest;
			out.accel = p.flyAccel;
			out.vMax = 0;
			_stillT += _dt;
			if (_stillT >= p.navStill - kTiny && _stage < 4) {
				NextStage("still");
				_stillT = 0;
			}
		} else {
			_stillT = 0;
		}
	}

	// R6 step 9 ([R6] Route 1, ARCH S4 part 1): the runtime's Router + TRAVEL dashes drive this hidden target. The old reach
	// chain (NAV / RAY / AI / push / door AI / BLINK and its stage clocks) is suspended for it: no stage advances, no reach blink,
	// no AI chase. The progress clocks stay fresh (the Router's stuck ladder reports a failure: Facts::route.none / owns 0).
	// When the route lets go (seen with a clear line, CHAIN, a failure) the chain starts fresh (stage -1) like on a new pick.
	void Brain::RouteHidden(const Facts& f, const Params& p, Out& out, const Cand& c)
	{
		++_st.routeFrames;
		if (_routeId != c.id) {
			_routeId = c.id;
			++_st.routeTakes;
			_nav = NavState{};
			_triedNav = _triedRay = _doorAi = _aiTried = false;
			_planFor = 0;
		}
		_stage = -1;
		_stageAt = _now;
		_stillT = 0;
		_aiProgAt = _pushRealProgAt = _now;
		_noPathSince = _exhaustedSince = -1;
		_stuckSince = -1;
		_progAt = _farProgAt = _now;  // the route's own stuck ladder judges progress
		_hiddenT = 0;                 // (the hard cap navHiddenHard still counts _hiddenTotal)
		out.move = Move::kNav;
		out.reachStage = kRouteStage;
		out.stageWhy = "route";
		out.aim = c.chest;
		out.aimVel = c.vel;
		out.vDes = {};  // the Travel pilot moves him (TABS dashes), never the brain's velocity laws
		out.accel = p.flyAccel;
		out.vMax = 0;
		out.planKind = 0;
		(void)f;
	}

	// R6 step 9: every planner of the Router failed on this target (no navmesh way, no air leg / punch spot / sky way; a locked,
	// quest-locked or load door with no other way). Indoors, toward an attacker of the player that the D2 defend rules allow
	// (d2DefendBlink: it hit the player within d2DbHitWin s, or the player is below d2CageHp), the BLINK stage at once (the
	// permitted defend jump until step 10); else the chain ends now (stage 4: the pick loop gives it up / reports the reach
	// failure) - the next foe within a frame instead of the NAV -> RAY -> AI -> BLINK walk.
	void Brain::RouteNone(const Facts& f, const Params& p, const Ctx& cx, IWorld& w, Out& out, const Cand& c, const Info& in)
	{
		if (_routeId != 0) _routeId = 0;
		// (R6 step 10, Facts::tele: any attacker of the player with no way is shielded at the BLINK stage, indoors or out)
		const bool hurt = (!f.ext.on && p.d2DefendBlink >= 0.5 && f.interiorCell && c.attackingPlayer &&
		                   (_now - c.lastHitPlayerAt <= p.d2DbHitWin || cx.health < p.d2CageHp)) ||
		                  (TeleOn(f) && TeleCase(c, f));
		if (_stage < 3) {
			++_st.routeNones;
			_triedNav = _triedRay = _aiTried = true;
			if (hurt) {
				++_st.routeNoneBlinks;
				StageTo(3, "routeNone");
			} else {
				StageTo(4, "routeNone");
			}
		}
		if (_stage == 3) {
			ReachHidden(f, p, cx, w, out, c, in);  // the BLINK stage (D2 defend rules); it ends at stage 4 like before
			return;
		}
		out.reachStage = _stage;
		out.stageWhy = _stageWhy;
		out.move = Move::kHold;
		out.vDes = {};
		out.aim = f.meChest;
		out.accel = p.flyAccel;
		out.vMax = 0;
	}

	// stage order: the first of NAV / RAY not tried yet (sky-first starts at RAY), then AI, BLINK, give up (4)
	void Brain::StageTo(int a_stage, const char* a_why)
	{
		++_st.stageAdv;
		_stage = std::clamp(a_stage, 0, 4);
		_stageAt = _now;
		_aiBest = 1e18;
		_aiProgAt = _now;
		_stageWhy = a_why;
		_stillT = 0;
	}

	void Brain::NextStage(const char* a_why)
	{
		++_st.stageAdv;
		if (_stage == 0) _triedNav = true;
		if (_stage == 1) _triedRay = true;
		if (_stage == 2) _aiTried = true;  // ROUND4 fix 2: left the AI stage by any rule (visNoProg too): the AI was tried
		if (_stage <= 1) _stage = !_triedNav ? 0 : !_triedRay ? 1 : (_aiTried ? 3 : 2);
		else _stage = std::min(4, _stage + 1);
		_stageAt = _now;
		_aiBest = 1e18;
		_aiProgAt = _now;
		_stageWhy = a_why;
	}

	// R6 s10 fix lane 3 (brain.retCrumbs; gate s10 M06 10.94 s, r5 museum 16:47:24-35: after the kill he had to get back out
	// of the gallery he had flown into over the balcony; the navmesh knows only the floor way, through the locked door
	// 000509AD, and he pressed on that door 8 s): his own flown trail - a point every crumbStep u of his chest, the last
	// kCrumbs, cleared by any jump - is a way that is known to be free. Pure: positions only, no engine call.
	void Brain::CrumbClear()
	{
		_crumbTop = _crumbN = 0;
		_crumbing = false;
		_crumbPrevOk = false;
	}

	void Brain::CrumbNote(const Facts& f, const Params& p)
	{
		if (p.retCrumbs < 0.5 || !IsFinite(f.meChest)) return;
		// a jump (his own blink, a put-back, a load door, the engine's catch-up warp): the trail before it leads nowhere from here
		if (_crumbPrevOk && LengthSq(f.meChest - _crumbPrev) > 400.0 * 400.0) CrumbClear();
		_crumbPrev = f.meChest;
		_crumbPrevOk = true;
		if (_crumbing) return;  // (flying it back: the trail is used up from its newest end)
		const double step = std::max(16.0, p.crumbStep);
		if (_crumbN > 0 && LengthSq(f.meChest - _crumbs[(_crumbTop + kCrumbs - 1) % kCrumbs]) < step * step) return;
		_crumbs[_crumbTop] = f.meChest;
		_crumbTop = (_crumbTop + 1) % kCrumbs;
		if (_crumbN < kCrumbs) ++_crumbN;
	}

	bool Brain::CrumbBack(const Facts& f, const Params& p, IWorld& w, Out& out, const Vec3& a_aimChest, double a_vcap, bool a_shutNow)
	{
		if (p.retCrumbs < 0.5) {
			_crumbing = false;
			return false;
		}
		// a trail flight not driven in the last frames (a target came, the return ended or was restarted elsewhere) is over
		if (_crumbing && (_now - _crumbAt > 0.25 || _now < _crumbAt)) _crumbing = false;
		auto newest = [&]() -> const Vec3& { return _crumbs[(_crumbTop + kCrumbs - 1) % kCrumbs]; };
		auto pop = [&]() {
			_crumbTop = (_crumbTop + kCrumbs - 1) % kCrumbs;
			--_crumbN;
			_crumbBest = 1e18;
			_crumbProgAt = _now;
		};
		if (!_crumbing) {
			// start: the navmesh way back just died at a locked / dead door, or the mover held him (FB) retCrumbAfter s with the
			// return getting no closer and no clear line to the spot (a clear ray past a low obstacle is the push's case); at most
			// once per retCrumbGap s
			// (a trail of fewer than 4 points - a few moves since his last jump, e.g. inside the room he blinked into: sp_tests A2-9 -
			// leads nowhere: the old way, the put-back request at once)
			if (_crumbN < 4 || (_now - _crumbTryAt < p.retCrumbGap - kTiny && _now >= _crumbTryAt)) return false;
			const bool held = !_returnClear && f.blockedFor >= p.retCrumbAfter - kTiny && _now - _retProgAt >= p.retCrumbAfter - kTiny;
			if (!a_shutNow && !held) return false;
			// the trail must lead somewhere: one of up to 8 of its points (newest to oldest) in a clear line to the player's spot (the
			// museum balcony over the gallery wall); none = he came in by a way the trail does not hold (a blink into a sealed room,
			// sp_tests A2-9): the old way at once
			_rayCap = std::max(_rayCap, static_cast<int>(p.rayBudgetBlink));
			const int step = std::max(1, _crumbN / 8);
			bool      leads = false;
			for (int k = 1; k <= _crumbN && !leads && CanCast(1); k += step)
				leads = RClear(w, _crumbs[(_crumbTop + kCrumbs - k) % kCrumbs], a_aimChest);
			if (!leads && CanCast(1)) leads = RClear(w, _crumbs[(_crumbTop + kCrumbs - _crumbN) % kCrumbs], a_aimChest);  // (the oldest)
			_crumbTryAt = _now;
			if (!leads) return false;
			_crumbing = true;
			_crumbSince = _crumbTryAt = _crumbProgAt = _now;
			_crumbBest = 1e18;
			_crumbRayAt = -1e9;
			++_st.crumbBacks;
		}
		auto end = [&](std::uint64_t& a_n) {
			++a_n;
			_crumbing = false;
			_retProgAt = _now;  // the return's own escalation (push, put-back request) restarts from here
			return false;
		};
		// the player's spot in a clear line: straight there (the return's direct flight; a fresh navmesh plan if it closes again)
		if (_now - _crumbRayAt >= 0.1 - kTiny && CanCast(1)) {
			_crumbRayAt = _now;
			if (RClear(w, f.meChest, a_aimChest)) {
				_returnClear = true;
				_returnCheckAt = _now;
				_retEsc = 0;
				_retBest = 1e18;
				if (_nav.forId == kReturnNavId) _nav = NavState{};
				return end(_st.crumbClear);
			}
		}
		const double reach = std::max(48.0, 0.75 * p.crumbStep);
		while (_crumbN > 0 && Length(newest() - f.meChest) <= reach) pop();
		if (_crumbN <= 0) return end(_st.crumbOut);
		if (_now - _crumbSince >= p.retCrumbMax - kTiny) return end(_st.crumbCapped);
		const Vec3   e = newest() - f.meChest;
		const double dist = Length(e);
		if (dist < _crumbBest - 10.0) {
			_crumbBest = dist;
			_crumbProgAt = _now;
		} else if (_now - _crumbProgAt >= p.retCrumbNoProg - kTiny) {
			return end(_st.crumbNoProg);  // something closed the way he came (a door swung shut, a body)
		}
		// speed: by the trail left in the next two points (consecutive points are a free flight he made)
		double ahead = dist;
		for (int k = 1; k <= 2 && k < _crumbN; ++k)
			ahead += Length(_crumbs[(_crumbTop + kCrumbs - k - 1) % kCrumbs] - _crumbs[(_crumbTop + kCrumbs - k) % kCrumbs]);
		const double vmax = std::min(a_vcap, (f.interiorCell || f.lowCeiling) ? p.navVMaxIndoor : p.navVMax);
		const double sp = std::min(vmax, std::max(p.navVMin, 3.0 * ahead));
		out.move = Move::kReturn;
		out.vDes = dist > 1e-3 ? e * (sp / dist) : Vec3{};
		out.aim = newest();
		out.aimVel = {};
		out.accel = p.navAccel;
		out.vMax = vmax;
		out.face = FlatDir(e);
		out.planKind = 6;  // (logs: 4 nav, 5 push, 6 the trail back)
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		_crumbAt = _now;
		_retProgAt = _now;
		_returnNoPathSince = -1;
		++_st.crumbFrames;
		return true;
	}

	void Brain::ReturnMove(const Facts& f, const Params& p, const Ctx& cx, IWorld& w, Out& out)
	{
		const Vec3   spot = f.haveFollowSpot ? f.followSpot : f.playerFeet;
		const double dSpot = Length(spot - f.meFeet);
		// ROUND3 SPEED (S4): the native follow flies straight at the spot. Geometry in the way for followBlockedFor s (one ray
		// per followRayEvery s) -> RETURN from returnBlockedMin u: sky path / side detour on the capsule-led far leg, instead
		// of pressing on the wall until the stuck put-back (live 17:16:08-10: 2.5 s, gap 362 -> 824 u).
		// ROUND3 fix (review, interior warp risk): in a real interior cell the blocked-follow RETURN only starts when the
		// navmesh can route it round the corner; without nav he stays on the native follow and the leash-snap AI walk
		// (runtime brain.leashFollowSuspendInterior), as in round 2, instead of hovering into the Papyrus put-back.
		const bool navOk = p.nav >= 0.5 && p.returnNav >= 0.5 && w.NavAvailable();
		// R6 s10 fix lane 3 (brain.retShutHold): the navmesh way back died at a locked / dead door less than retShutHold s ago -
		// it is not planned again meanwhile (a refused put-back restarted the same leg into the same door 18 times in 8 s)
		const bool navShut = p.retShutHold > 0 && _now < _retNavShutUntil && _retNavShutUntil - _now <= p.retShutHold + kTiny;
		const bool navOkR = navOk && !navShut;
		const bool blockedOk = !f.interiorCell || navOk;
		if (!_returning && blockedOk && f.followActive && !f.waiting && dSpot > p.returnBlockedMin) {
			if (_now - _followRayAt >= p.followRayEvery - kTiny && CanCast(1)) {
				_followRayAt = _now;
				const double up = std::max(0.0, f.meChest.z - f.meFeet.z);
				_followRayBlocked = !RClear(w, f.meChest, spot + Vec3{ 0, 0, up });
			}
			_followBlockedT = _followRayBlocked ? _followBlockedT + cx.dt : 0.0;
		} else if (!_returning) {
			_followBlockedT = 0;
			_followRayBlocked = false;
		}
		const bool blockedStart = _followBlockedT >= p.followBlockedFor - kTiny && _now >= _blockedReturnUntil;
		// DPREP-FOLLOW (tabs-spec G10 "never push toward a point he cannot see"; live 'sim held at a wall' ~2600/min while
		// following near buildings): the runtime found no follow spot with a clear line from his SIM torso, or its sweep held
		// the sim at a wall (runtime.followHeldReanchor), and stopped pushing; the navmesh takes him to the spot at once, from
		// returnDoneNear (the brain's own capsule ray above waits followBlockedFor and starts only beyond returnBlockedMin).
		// brain.followBlockedNav 0 = round 5 B.
		if (p.followBlockedNav >= 0.5 && f.followBlocked && !_returning && blockedOk && f.followActive && !f.waiting && dSpot > p.returnDoneNear &&
			cx.dMePlayer <= p.followMax && _now >= _blockedReturnUntil && _now >= _noReturnUntil) {
			StartBlockedReturn(dSpot, navOk, true, 0);
			++_st.blockedReturns;
		}
		if (!_returning && !f.waiting && (cx.dMePlayer > p.followMax || blockedStart) && _now >= _noReturnUntil) {
			_returning = true;
			_returnBlocked = !(cx.dMePlayer > p.followMax);
			if (_returnBlocked) {
				_returnClear = false;  // the default / last value is true: a blocked return must not end on its first frame
				++_st.blockedReturns;
			}
			_returnSince = _now;
			_returnNoPathSince = -1;
			_returnCheckAt = -1e9;
			if (_nav.forId == kReturnNavId) _nav = NavState{};  // a fresh navmesh plan for every return
			_retBest = dSpot;  // ROUND5: the return's own progress clock
			_retProgAt = _now;
			_retEsc = 0;
			_pushFor = 0;
			_returnStuck = false;  // ROUND5 A2
			_retStart = dSpot;
			_retEndOff = _retWait = false;  // FINISH B2 Fable
			// DPREP-FOLLOW: a blocked follow flies the navmesh back first (not the sky / side plan into the same wall)
			if (_returnBlocked && p.followBlockedNav >= 0.5 && navOkR) _retEsc = 1;  // (s10 fix lane 3: not while the way is shut)
		}
		// a blocked-follow return ends as soon as its own line check is clear within returnDone, or at returnDoneNear
		// ROUND5: "back" also means at his level (returnDoneDz): on a roof 580 u over the player the return ended and the
		// native follow (runtime.followMaxZ 250) never took over - he stayed up there
		const bool dzOk = p.returnDoneDz <= 0 || std::abs(spot.z - f.meFeet.z) <= p.returnDoneDz;
		// ROUND5 A2: a stuck-follow return (Update) ends at returnDoneNear, or with a clear line once it got 120 u closer (the
		// line was clear while he pressed on something low: the line alone is no proof he can follow again)
		const bool clearDone = _returnClear && dSpot <= p.returnDone && (!_returnStuck || _retBest <= _retStart - 120.0);
		const bool doneBlocked = dzOk && _returnBlocked && (dSpot <= p.returnDoneNear || clearDone);
		if (_returning && ((dzOk && !_returnBlocked && dSpot <= p.returnDone) || doneBlocked || f.waiting)) {
			if (_returnBlocked) _blockedReturnUntil = _now + p.returnBlockedGap;  // no return <-> follow flapping
			_pbSeenSince = -1e9;  // DPREP-FOLLOW: back: the seen put-back clock restarts
			_d2PbSince = -1e9;    // D2: back: the put-back episode is over
			_d2PbN = 0;
			_followBlockedT = 0;
			_followRayBlocked = false;
			_returnStuck = false;  // ROUND5 A2
			_retEndOff = _retWait = false;  // FINISH B2 Fable
			_returning = _recallFly = _returnBlocked = false;
			_returnSince = _returnNoPathSince = -1;
			if (_planForReturn) _planFor = 0;
		}
		if (!_returning) {
			out.move = Move::kFollow;
			out.hunt = HuntState::kIdle;
			_wasFarLeg = false;
			return;
		}
		if (_returnSince < 0) _returnSince = _now;
		out.hunt = HuntState::kReturn;
		const double vcap0 = f.playerSneaking ? std::min(p.huntVMax, p.sneakReturnV) : p.huntVMax;
		const double vcap = f.indoors ? std::min(vcap0, p.indoorMaxSpeed) : vcap0;
		const double chestUp = std::max(0.0, f.meChest.z - f.meFeet.z);
		const Vec3   aimChest = spot + Vec3{ 0, 0, chestUp };
		if (_now - _returnCheckAt >= p.returnCheckEvery - kTiny && CanCast(1)) {
			_returnCheckAt = _now;
			_returnClear = RClear(w, f.meChest, aimChest);
		}
		// ROUND5 (in game round 4 R2, Concord: after the deathclaw kill the sky path back put him on the museum roof, 1300 u
		// over the player, and he pressed on it for 7+ s with vDes ~200 and no put-back; the museum return through the locked
		// door ended in "putback why=1"): the return keeps its own progress clock (closer to the spot by progressStep). No
		// progress for returnNoProg s: the navmesh leg back (exteriors too), then the flown push; the put-back comes only after
		// returnPushFor s of pushing without progress. brain.navFlyPush 0: round 4.
		const bool push = p.navFlyPush >= 0.5;
		if (dSpot < _retBest - p.progressStep) {
			_retBest = dSpot;
			_retProgAt = _now;
		}
		// A valid stair / corridor route can get longer in straight-line distance while getting shorter along the route.
		// Do not abandon it for a direct push at the wall. progAt also advances on failed replans: movedAt deliberately does not.
		if (p.retNavProgress >= 0.5 && _retEsc <= 1 && _nav.forId == kReturnNavId && _nav.ok && !_nav.dead &&
			_nav.movedAt >= _returnSince && _nav.movedAt > _retProgAt && Length(Flat(spot - _nav.goal)) <= p.navGoalMove) {
			_retProgAt = _nav.movedAt;
			++_st.retNavProgressKeeps;
		}
		if (push && _retEsc < 2 && _now - _retProgAt >= p.returnNoProg - kTiny) {
			_retEsc = (_retEsc == 0 && navOkR) ? 1 : 2;  // (s10 fix lane 3: navOkR - not while the way is shut)
			_retProgAt = _now;
			++(_retEsc == 1 ? _st.returnNavs : _st.returnPushes);
			if (_retEsc == 1 && _nav.forId == kReturnNavId) _nav = NavState{};
		}
		// FINISH B2 Fable (brain.retEndWait; gate finA2 K03 17.2 vs 8.1 per 10 min - 44 of the 79 soak stretches were this: the ArcJet in1
		// tour puts the player on the mezzanine, (0,569,189), 180 u off the navmesh he can reach; the way back's leg ends at (240,332,73)
		// with no clear line to the spot; the push sidestepped at pushV across the building, the refused put-back - the player sees him -
		// restarted leg -> push -> sidestep every 3-8 s for 25 s: GK4 21:36:16-41, GK5 22:19:31-46, pressed 1-2.7 s each time; the
		// installed build stands at the same spot). Indoors, with the leg's goal off the mesh (navOffGoal) and him at the leg's end: once
		// the mover holds him pressed retEndWait s (or both sidesteps are spent) he WAITS at the mesh end, facing the player, with no mover
		// intent (the FB clock cannot run) - until the player's spot moves navGoalMove u (a fresh leg, as when the tour walks on), the line
		// to the spot clears (the direct flight: the player at the edge) or the put-back is allowed (unseen / d2PutBackSeenMax, unchanged);
		// a refused put-back keeps the wait (RequestPutBack). A target ends the return as always (the hunt owns him).
		if (p.retEndWait > 0 && f.interiorCell && push) {
			const bool atEnd = _nav.forId == kReturnNavId && _nav.ok && !_nav.wp.empty() && _nav.floor.size() == _nav.wp.size() &&
			                   Length(_nav.floor.back() - spot) > p.navOffGoal + kTiny &&
			                   _nav.idx >= static_cast<int>(_nav.wp.size()) - 1 &&
			                   Length(_nav.wp.back() - f.meChest) <= std::max(p.navEndReach, p.navReach) + kTiny;
			if (atEnd && !_retEndOff) {
				_retEndOff = true;
				_retWaitEnd = _nav.wp.back();
				_retWaitGoal = spot;
			}
			if (_retEndOff) {
				const bool moved = Length(spot - _retWaitGoal) > p.navGoalMove + kTiny;
				if (moved || _returnClear) {
					if (_retWait) ++(moved ? _st.retEndMoves : _st.retEndClears);
					_retEndOff = _retWait = false;
					_retEsc = _returnClear ? 0 : 1;
					_retProgAt = _now;
					_retBest = dSpot;
					_pushFor = 0;
					if (!_returnClear && _nav.forId == kReturnNavId) _nav = NavState{};
				} else {
					const bool pressedLong = f.blockedFor >= p.retEndWait - kTiny || (_retEsc >= 2 && _pushSwaps >= 2) ||
					                         (_retEsc >= 2 && f.blockedFor > 0.0 && _now - _pushRealProgAt >= p.retEndWait - kTiny);
					if (!_retWait && pressedLong) {
						_retWait = true;
						_retWaitAt = _now;
						++_st.retEndWaits;
						if (_retEsc < 2) {
							_retEsc = 2;
							++_st.returnPushes;
						}
					}
					if (_retWait) {
						// at the mesh end: hover there (no intent); carried off by the push: back to it at navVMin
						const Vec3   e = _retWaitEnd - f.meChest;
						const double dist = Length(e);
						const bool   back = dist > 1.5 * p.navReach;
						out.move = Move::kReturn;
						out.vDes = back ? e * (std::min(p.navVMin, 3.0 * dist) / dist) : Vec3{};
						out.aim = back ? _retWaitEnd : f.meChest;
						out.aimVel = {};
						out.accel = p.navAccel;
						out.vMax = std::min(vcap, p.navVMaxIndoor);
						out.face = FlatDir(spot - f.meFeet);
						out.planKind = 7;  // (logs: 4 nav, 5 push, 6 the trail back, 7 the wait at the mesh end)
						out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
						_returnNoPathSince = -1;
						_wasFarLeg = false;
						++_st.retEndWaitFrames;
						// the put-back escalation as before (returnPushFor s without progress / returnMax s): refused while the player sees
						// him (the wait goes on), allowed when it was allowed before
						const bool pushStuck = _now - _retProgAt >= p.returnPushFor - kTiny;
						const bool tooLong = _now - _returnSince >= p.returnMax;
						if (pushStuck || tooLong) RequestPutBack(PutBackWhy::kReturnFailed, out);
						return;
					}
				}
			}
		} else {
			_retEndOff = _retWait = false;
		}
		bool flying = false, navLeg = false;
		auto pendingHold = [&]() {
			out.move = Move::kReturn;
			out.vDes = {};
			out.aim = f.meChest;
			out.aimVel = {};
			out.accel = p.navAccel;
			out.vMax = 0;
			out.face = FlatDir(spot - f.meFeet);
			out.planKind = 4;
			++_st.retPendingHolds;
		};
		auto navBack = [&]() {
			Cand goal;
			goal.id = kReturnNavId;
			goal.feet = spot;
			goal.chest = aimChest;
			goal.height = std::max(64.0, chestUp / 0.68);
			goal.head = spot + Vec3{ 0, 0, goal.height };
			goal.vel = f.playerVel;
			return FollowNav(f, p, w, out, goal);
		};
		auto pushBack = [&]() {
			FlyPush(f, p, w, out, aimChest, kReturnNavId);
			out.move = Move::kReturn;
			out.vMax = std::min(out.vMax, vcap);
			_returnNoPathSince = -1;
			flying = true;
			navLeg = true;  // no terrain climb: it slides along what is in the way
		};
		// the navmesh leg back has no path from here (dead): the push for good; a plan still pending / a door window: the
		// push for this frame only
		auto navDead = [&]() { return !navOkR || (_nav.forId == kReturnNavId && _nav.dead); };
		// R6 s10 fix lane 3 (brain.retShutHold / retCrumbs): the navmesh leg back just died at a locked / dead door -> that way is
		// shut for retShutHold s; and the trail flight back (his own flown way) goes first, before any put-back request
		// FINISH A2 fix lane 2 r2 (brain.retShutLocked; gate finA K03: all 14 ways back shut in the 6 finA soaks were ArcJet's OPEN stair-top
		// doorways 000DCF41 / 000DD113 ('DOOR|skip ... (notClosed)', his body grazing the frame twice = doorDead), shut 8 s again and again
		// while the player toured - 18 of the 33 return stuck episodes fell in those windows; the installed build has no such shut): only
		// a LOCKED door shuts the way back (the museum's 000509AD, the rule's own case); an open doorway is planned again
		auto noteShut = [&]() {
			if (p.retShutHold > 0 && p.retShutLocked >= 0.5 && _nav.forId == kReturnNavId && _nav.dead && _nav.doorDead && !_nav.locked && _now >= _retNavShutUntil &&
				!(_now >= _retOpenAt - kTiny && _now - _retOpenAt < p.retShutHold - kTiny)) {
				_retOpenAt = _now;
				++_st.retOpenKeeps;  // (the s10 rule would have shut this open doorway's way back for retShutHold s)
			}
			if (!(p.retShutHold > 0 && _nav.forId == kReturnNavId && _nav.dead && (_nav.locked || (_nav.doorDead && p.retShutLocked < 0.5)))) return false;
			if (_now >= _retNavShutUntil) ++_st.retShuts;
			_retNavShutUntil = _now + p.retShutHold;
			return true;
		};
		auto crumbFrame = [&]() {
			if (!_wasFarLeg && f.meOnGround) out.launch = true;
			_wasFarLeg = true;
		};
		if (CrumbBack(f, p, w, out, aimChest, vcap, false)) {  // a trail flight goes on, or the mover held him (FB) without progress
			crumbFrame();
			return;
		}
		// ROUND5 A2: the way back is shut by a locked door (the museum: 000509AD is not forced any more) - a push at it gets
		// nowhere (c120-r4m: 2 s of pushing at the open door's frame, then "putback why=1" anyway): the put-back at once
		auto lockedBack = [&]() {
			if (!(push && p.lockedBlink >= 0.5 && _nav.forId == kReturnNavId && _nav.dead && _nav.locked)) return false;
			RequestPutBack(PutBackWhy::kReturnFailed, out);
			out.move = Move::kReturn;
			out.vDes = {};
			out.aim = f.meChest;
			out.accel = p.huntAccel;
			out.vMax = vcap;
			out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
			return true;
		};
		if (push && _retEsc == 1) {
			navLeg = navOkR && navBack();
			if (navLeg) {
				flying = true;
				_returnNoPathSince = -1;
				++_st.navReturns;
				out.move = Move::kReturn;
				out.vMax = std::min(out.vMax, vcap);
			} else if (navDead()) {
				if (noteShut() && CrumbBack(f, p, w, out, aimChest, vcap, true)) {  // s10 fix lane 3
					crumbFrame();
					return;
				}
				if (lockedBack()) return;
				_retEsc = 2;
				++_st.returnPushes;
			}
		}
		if (push && _retEsc == 1 && !navLeg && navOkR && !navDead() && p.retNavPending >= 0.5) {
			pendingHold();
		} else if (push && (_retEsc >= 2 || (_retEsc == 1 && !navLeg))) {
			pushBack();
		} else if (push && _retEsc == 1) {
			// the navmesh leg above
		} else if (_returnClear) {
			_returnNoPathSince = -1;
			if (_planForReturn) _planFor = 0;
			const CruiseCfg cc{ vcap, p.huntAccel, std::min(p.returnHandV, vcap), p.returnDone, p.huntFF, p.huntBrake };
			out.move = Move::kReturn;
			out.aim = aimChest;
			out.aimVel = f.playerVel;
			out.vDes = CruiseVDes(f.meChest, aimChest, f.playerVel, cc);
			out.accel = p.huntAccel;
			out.vMax = vcap;
			out.face = FlatDir(out.vDes);
			flying = true;
		} else if (!f.interiorCell) {  // ROUND3 SPEED (S5): porches / eaves get the sky / side path too
			flying = FollowPlan(f, p, cx, w, out, nullptr, spot, aimChest, chestUp, true);
			out.move = Move::kReturn;
			if (!flying) {
				if (_returnNoPathSince < 0) _returnNoPathSince = _now;
			} else {
				_returnNoPathSince = -1;
			}
		} else {
			// ROUND3 fix (review): a real interior and the line is blocked -> the navmesh path round the corner (the same
			// FollowNav legs as a hunt; the player is the goal). No usable path: hover; the no-path rule hands him to the
			// Papyrus put-back (navmesh spot), as in round 2.
			if (navOkR) {  // (s10 fix lane 3: not while the way back is shut)
				Cand goal;
				goal.id = kReturnNavId;
				goal.feet = spot;
				goal.chest = aimChest;
				goal.height = std::max(64.0, chestUp / 0.68);
				goal.head = spot + Vec3{ 0, 0, goal.height };
				goal.vel = f.playerVel;
				navLeg = FollowNav(f, p, w, out, goal);
			}
			if (navLeg) {
				flying = true;
				_returnNoPathSince = -1;
				++_st.navReturns;
				out.move = Move::kReturn;
				out.vMax = std::min(out.vMax, vcap);
			} else if (push) {
				if (noteShut() && CrumbBack(f, p, w, out, aimChest, vcap, true)) {  // s10 fix lane 3
					crumbFrame();
					return;
				}
				if (lockedBack()) return;  // ROUND5 A2
				// ROUND5: no usable navmesh path back: he flies at the spot himself (slides along the walls), no hover
				if (_retEsc < 2 && navDead()) {
					_retEsc = 2;
					_retProgAt = std::min(_retProgAt, _now);
					++_st.returnPushes;
				}
				if (p.retNavPending >= 0.5 && !navDead()) pendingHold();
				else pushBack();
			} else {
				out.move = Move::kReturn;
				out.vDes = {};
				out.aim = f.meChest;
				out.accel = p.huntAccel;
				out.vMax = vcap;
				out.face = FlatDir(spot - f.meFeet);
				if (_returnNoPathSince < 0) _returnNoPathSince = _now;
			}
		}
		// no terrain-clearance climb on a navmesh leg (it would lift him into ceilings and door lintels)
		if (flying && !navLeg) FarLegExtras(f, p, w, out, out.aim);
		if (flying && !_wasFarLeg && f.meOnGround) out.launch = true;
		_wasFarLeg = flying;
		out.dvMax = out.accel * std::clamp(f.dt, 0.0, 0.1);
		const bool noPath = _returnNoPathSince >= 0 && _now - _returnNoPathSince >= p.returnNoPath - kTiny;
		const bool tooLong = _now - _returnSince >= p.returnMax;
		if (push) {
			// ROUND5: an exterior plan that found nothing pushes too (the no-path hover was the put-back's waiting room)
			if (noPath && _retEsc < 2) {
				_retEsc = 2;
				_retProgAt = _now;
				_returnNoPathSince = -1;
				++_st.returnPushes;
			}
			const bool pushStuck = _retEsc >= 2 && _now - _retProgAt >= p.returnPushFor - kTiny;
			if (pushStuck || tooLong) RequestPutBack(PutBackWhy::kReturnFailed, out);
		} else if (noPath || tooLong) {
			RequestPutBack(PutBackWhy::kReturnFailed, out);
		}
	}

	// ------------------------------------------------------------------------------------------- vetting (K4)
	void Brain::BuildVet(const Facts& f, const Params& p, const Ctx& cx)
	{
		_vet.clear();
		const auto&  cs = f.cands;
		const int    n = static_cast<int>(cs.size());
		const size_t vmax = static_cast<size_t>(std::max(0.0, p.vetMax));
		if (vmax == 0) return;
		if (_target && (IsFar(_tTier) || _tTier == Tier::kPreempt)) _vet.push_back(_target);  // re-approval while it lasts
		struct Row
		{
			int    i;
			int    rank;
			double d;
		};
		Row farRows[32];
		Row preRows[32];
		Row aheadRows[32];  // ROUND3 fix 5: pre-vet (hidden / just beyond reach first strikes), after the others
		int nf = 0, np = 0, na = 0;
		for (int i = 0; i < n; ++i) {
			const Info& in = _info[i];
			if (cs[i].id == _target || in.m == nullptr) continue;
			const bool notFresh = in.m->clearedUntil - cx.now < p.vetFresh;
			const Tier t = in.pending ? in.pendTier : (in.pickable && notFresh ? in.tier : Tier::kNone);
			if (IsFar(t) && nf < 32) farRows[nf++] = { i, FarRank(t), in.dPlayer };
			else if (t == Tier::kPreempt && np < 32) preRows[np++] = { i, 0, in.dPlayer };
			else if (t == Tier::kNone && in.preVet && notFresh && na < 32) aheadRows[na++] = { i, 0, in.dPlayer };
		}
		std::sort(farRows, farRows + nf, [](const Row& a, const Row& b) { return a.rank != b.rank ? a.rank > b.rank : a.d < b.d; });
		std::sort(preRows, preRows + np, [](const Row& a, const Row& b) { return a.d < b.d; });
		std::sort(aheadRows, aheadRows + na, [](const Row& a, const Row& b) { return a.d < b.d; });
		for (int k = 0; k < nf && k < static_cast<int>(p.vetFar) && _vet.size() < vmax; ++k) _vet.push_back(cs[farRows[k].i].id);
		for (int k = 0; k < np && k < static_cast<int>(p.vetPreempt) && _vet.size() < vmax; ++k) _vet.push_back(cs[preRows[k].i].id);
		// the regular Papyrus pass (every ~0.3 s) vets these too, so a first strike finds its token ready when he sees it;
		// they never trigger the fast request themselves
		for (int k = 0; k < na && k < static_cast<int>(p.vetPre) && _vet.size() < vmax; ++k) {
			_vet.push_back(cs[aheadRows[k].i].id);
			++_st.preVets;
		}
	}

	// ------------------------------------------------------------------------------------------- the frame
	void Brain::Update(const Facts& f, const Params& p, IWorld& w, Out& out)
	{
		out = Out{};
		_p = p;
		_now = f.now;
		if (_atPlRecovery.pending && f.now > _atPlRecovery.requestedAt + kTiny) {
			// No DidBlink callback arrived: the runtime refused the shortcut. The old route still owns the next frame.
			auto& r = _atPlRecovery;
			if (_target == r.target) {
				_nav = std::move(r.nav);
				_stage = r.stage; _stageAt = r.stageAt; _stageWhy = r.why;
				_aiBest = r.aiBest; _aiProgAt = r.aiProgAt; _stillT = r.stillT;
				_triedNav = r.triedNav; _doorAi = r.doorAi;
				++_st.atPlRouteKeeps;
			}
			r.pending = false;
		}

		_curF = &f;  // DPREP-FOLLOW: RequestPutBack's view check
		_curW = &w;
		const double now = f.now;
		if (_resetPending) {  // completion side lane 4 s13r2 (brain.newcomerKnown): the first Update after a Reset
			_resetPending = false;
			_resetAt = now;
		}
		const double dt = std::clamp(std::isfinite(f.dt) ? f.dt : 0.0, 0.0, 0.1);
		_dt = dt;
		// D1H (hotfix-2 smoke F4, s2-hf120 HF5: 3 knock-downs of one essential in 57.5 s real time - the 60 s cap window ran
		// on the brain clock, 0.90-1.14x real time at 120 fps): the essential history runs on the wall clock when the glue
		// gives one. A switch of clocks clears the history (the old stamps are in the other clock's units).
		{
			const bool wallOn = p.essWallClock >= 0.5 && std::isfinite(f.wall) && f.wall >= 0.0 && std::isfinite(now);
			if (wallOn != _essWallOn) {
				for (auto& kv : _mem) std::fill(std::begin(kv.second.essAt), std::end(kv.second.essAt), -1e9);
				_essWallOn = wallOn;
			}
			_essOff = wallOn ? f.wall - now : 0.0;
		}
		++_st.updates;
		_rays = 0;
		_rayCap = static_cast<int>(p.rayBudget);
		// s12 r2 fix lane 1 (brain.fpArriveT): the player arrived somewhere new = the feet jumped more than kArriveJump u since the
		// last Update (a door, fast travel, a load; the soak Runner's segment moves) - Classify holds the far first strikes then
		if (std::isfinite(f.playerFeet.x) && std::isfinite(f.playerFeet.y) && std::isfinite(f.playerFeet.z)) {
			if (_plFeetHave && LengthSq(f.playerFeet - _plFeetPrev) > kArriveJump * kArriveJump) {
				_plArriveAt = now;
				++_st.playerArrivals;
			}
			_plFeetPrev = f.playerFeet;
			_plFeetHave = true;
		}
		_protNearCur = false;
		CrumbNote(f, p);  // R6 s10 fix lane 3 (brain.retCrumbs): his flown trail
		auto finish = [&]() {
			out.rays = _rays;
			out.rayCap = _rayCap;
			_st.rays += static_cast<std::uint64_t>(_rays);
			_st.raysMax = std::max(_st.raysMax, _rays);
			_st.huntT = _target ? now - _tSince : 0.0;
			_st.chainT = _chainT;
			_st.attackHolds = _attackHoldN;  // DPREP-T
			_st.switchHolds = _switchHoldN;
			_st.d2SlowSwitches = _d2SlowSwN;  // D2 tuner
			_st.d2DefendBreaks = _d2DbN;
			_st.s2fBandD = _s2fBandDN;  // step-2 fixer
			_st.s2fBig = _s2fBigN;
			_st.s2fDefBlinks = _s2fDefBlinkN;
			_st.cageGuards = _cageGuardN;  // s4 fix lane 2
			_st.cageSkips = _cageSkipN;
			_st.cageHopKeeps = _cageHopKeepN;  // s11 fix lane 1
			_st.bigAimSwitches = _bigAimN;     // s12 fix lane 2
			_st.fpFarOuts = _fpFarN;           // s12 r2 fix lane 1
			_st.fpArriveOuts = _fpArriveN;
			_st.fpHereKeeps = _fpHereN;        // FINISH A2 fix lane 2 r2 (brain.fpHereR)
			_st.protNearHolds = _protNearN;
			_st.atkKeeps = _atkKeepN;          // s12 fix r2 lane 2 (brain.atkKeepNear)
			_st.burstKeeps = _burstKeepN;      // s12 r3 fix lane 1 (brain.burstKeepA / burstHidR)
			_st.burstHidLast = _burstHidN;
			_st.blinkHolds = _blinkHoldN;
			_st.burstBigSkips = _burstBigN;  // s12 r3 integrator (brain.burstBigPassive)
			_st.burstChainPicks = _burstChainN;  // FINISH lane 6 (brain.burstChainPl)
			_st.bigTurnGroupPicks = _bigTurnGroupN;  // FINISH lane 6 (brain.bigTurnGroup)
			_st.bigTurnHurtWaits = _bigTurnHurtN;    // FINISH A2 lane 3 round 2 (brain.bigTurnRun: the turn path's waits)
			_st.bigTurnNearWaits = _bigTurnNearN;
			_st.chainNear = _chainNearN;     // s13 r3 lane 5 (brain.chainNearR)
			_st.flipKeeps = _flipKeepN;        // s12 fix r3 lane 3 (brain.atkFlipKeep / atkFlipNearR)
			_st.defCapBlinks = _capBlinkN;     // ... (brain.defendCapBlink)
			_st.preNoTokens = _preNoTokN;      // ... (brain.preemptNoTokenR)
			_st.d2EssSkips = _d2EssSkipN;
			_st.essHurtPicks = _essHurtPickN;  // completion side lane 1 (brain.essHurtFirst)
			_st.blinkRepeatDefOk = _repeatDefOkN;  // completion side lane 4 s13r2 (brain.blinkRepeatDefend / newcomerKnown / preemptAimKeep)
			_st.newcomerOld = _newcomerOldN;
			_st.newcomerOldBig = _newcomerOldBigN;  // completion side 3 s13r3 (brain.newcomerKnown: who was stamped / who came back fighting)
			_st.newcomerOldBigLast = _newcomerOldBigLast;
			_st.newcomerFight = _newcomerFightN;
			_st.preAimKeeps = _preAimKeepN;
			_st.fpDrops = _fpDropN;
			_st.protKeepThreat = _prot.keepThreat;  // R6 step 8.1
			_st.protKeepFlip = _prot.keepFlip;
			_st.protKeepDwell = _prot.keepDwell;
			_st.protHurter = _prot.hurter;
			_st.protSwitches = _prot.switches;
			_st.protKeepBig = _prot.keepBig;  // s12 fix lane 3
			_st.protBigReturns = _prot.bigReturns;
			_st.protBigFirst = _prot.bigFirst;  // s12 fix round 2 lane 3 (R7)
			_st.protKeepBigFirst = _prot.keepBigFirst;
			_st.protKeepCommit = _prot.keepCommit;  // s12 r2 lane 4 (RC)
			_st.protKeepCommitRuns = _prot.keepCommitRuns;
			_st.protCommitStallKeeps = _rcStallKeeps;
			_st.protBigOnce = _prot.bigOnce;  // s12 r3 lane 4 (R7b)
			_st.protKeepBigOnce = _prot.keepBigOnce;
			_st.protKeepBigOnceRuns = _prot.keepBigOnceRuns;
			_st.protCommitWallKeeps = _rcWallKeeps;  // s12 r3 lane 5
		};

		// L4D2 Body.Stuck()
		// ROUND4 fix 2 (in game R1, museum at 120 fps: stuckT 3.4 and a put-back while he moved at 250-530 u/s and got closer):
		// a contact does not count while he is getting somewhere - closer to the target by progressStep, or nav progress
		// (both refresh _progAt), within stuckProgGrace s, at >= stuckProgSpeed u/s. stuckProgGrace 0 = the L4D2 rule.
		bool contact = f.bodyContact;
		if (contact && p.stuckProgGrace > 0 && _target && now - _progAt < p.stuckProgGrace && f.actualSpeed >= p.stuckProgSpeed) {
			contact = false;
			++_st.progHolds;  // frames
		}
		// ROUND5: the flown push presses on walls on purpose (its forward tries); it has its own clocks (the chain's
		// navAiNoProg / navAiMax / hidden caps, the return's returnPushFor), so its contacts never feed the stuck put-back
		if (contact && p.navFlyPush >= 0.5 && now - _pushAt <= 2.0 * dt + kTiny) {
			contact = false;
			++_st.pushHolds;  // frames
		}
		_stuck.Step(contact, f.intentSpeed, f.actualSpeed, dt, p);
		_stuckFor = _stuck.Stuck(p) ? _stuckFor + dt : 0.0;
		_st.stuckT = _stuck.t;
		if (now - _lastPruneAt > 5.0) Prune(now);
		_lastIds.clear();
		_lastAtk.clear();
		_lastPlAtk.clear();
		for (const Cand& c : f.cands) {
			_lastIds.push_back(c.id);
			const double hitAny = now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt });
			if (hitAny <= p.attackerValid && GuardDist(c.feet, f.centre) <= p.attackerGuard) _lastAtk.push_back(c.id);
			// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR): the foes attacking the player near it (Recall keeps them pickable)
			if (p.recallKeepPlAtkR > 0 && c.attackingPlayer && c.hostile && GuardDist(c.feet, f.playerFeet) <= p.recallKeepPlAtkR + kTiny) _lastPlAtk.push_back(c.id);
		}

		// ROUND3 fix 2: the stale limit follows the measured push gap (staleGapMult x the average, staleAfter..staleAfterMax)
		const double staleAfter = std::clamp(p.staleGapMult * _verdictGap, p.staleAfter, std::max(p.staleAfter, p.staleAfterMax));
		const bool   stale = !_everVerdict || now - _lastVerdictAt > staleAfter;
		_st.stale = stale;
		_st.staleAfter = staleAfter;
		if (_endId) {  // a far target dropped between Updates (Recall / OnTeleport / Ignore)
			out.huntEnd = true;
			out.huntEndId = _endId;
			out.huntWhy = _endWhy;
			_endId = 0;
		}
		auto endHere = [&](const char* a_why) {
			if (_target && _tFar && !out.huntEnd) {
				out.huntEnd = true;
				out.huntEndId = _target;
				out.huntWhy = a_why;
			}
		};

		// idle: escort, down, paused / sent away, saving (SAFETY G3): no target, no movement, no rays
		if (f.escorting || f.meDown || f.paused || f.saving) {
			endHere(f.escorting ? "escort" : f.meDown ? "down" : f.saving ? "saving" : "paused");
			if (_target) DropTarget();
			_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
			_wasFarLeg = false;
			_vet.clear();
			out.move = Move::kFollow;
			out.hunt = HuntState::kIdle;
			finish();
			return;
		}

		Ctx cx;
		cx.now = now;
		cx.dt = dt;
		cx.centre = f.centre;
		cx.playerFeet = f.playerFeet;
		cx.R = Length(Flat(f.playerVel)) > p.movingSpeed ? p.guardMoving : p.guardStill;
		cx.waiting = f.waiting;
		cx.range = f.waiting ? 0 : std::clamp(static_cast<int>(std::lround(p.range)), 0, 2);
		cx.farR = p.FarR(f.uGrids);
		cx.maxR = p.MaxR(f.uGrids);
		cx.abort = p.HuntAbort(f.uGrids);
		cx.health = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
		cx.dialogue = f.playerDialogue;
		// ROUND3 (live 1706): quest scenes that run DURING a fight include the player (Preston in Concord, gate 8 for 17.5 s)
		// and closed every far tier and Preempt mid-battle. A scene only counts as "busy" while the player is not in combat;
		// dialogue always does. Rollback: brain.sceneCombatOpen = 0.
		const bool sceneBusy = f.playerScene && !(p.sceneCombatOpen >= 0.5 && f.playerInCombat);
		cx.busy = f.playerDialogue || sceneBusy;
		cx.sneaking = f.playerSneaking;
		cx.stale = stale;
		cx.staleNative = stale && _everVerdict && p.staleNative >= 0.5;
		cx.dMeCentre = GuardDist(f.meFeet, f.centre);
		cx.dMePlayer = GuardDist(f.meFeet, f.playerFeet);

		// hard abort (ROUND2 §4.4): farther than huntAbort from the player -> no flight back, Papyrus places him
		// R6 step 4 (Facts::ext.on): the lost transfer when the Warden's lost condition holds (ARCH §4.11: the player in another
		// cell / worldspace for > 3 s, or > 8000 u away with no progress for 30 s; RequestPutBack checks the view); beyond
		// huntAbort without it he is not placed (the abort branch hands him to the flown follow)
		if (!f.waiting && (cx.dMePlayer > cx.abort || (f.ext.on && f.ext.mayLost) || (TeleOn(f) && f.tele.mayLost))) {  // (R6 step 10: the Pilot's lost condition)
			endHere("abort");
			if (_target) DropTarget();
			_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
			_wasFarLeg = false;
			RequestPutBack(PutBackWhy::kLost, out);
			out.move = Move::kFollow;
			out.hunt = HuntState::kIdle;
			finish();
			return;
		}

		// far chain cap (K12): 30 s of continuous far hunting -> RETURN, then 3 s inside the ring
		if (_target && _tFar) {
			_chainT += dt;
			_restT = 0;
		} else if (cx.dMeCentre <= p.guardStill) {
			_restT += dt;
			if (_restT >= p.huntRest - kTiny) {
				_chainT = 0;
				_chainLock = false;
			}
		}
		if (!_chainLock && _chainT >= p.huntChainMax) {
			_chainLock = true;
			_restT = 0;
			++_st.chainRests;
			if (_target && _tFar) {
				out.giveUp = GiveUp::kChain;
				out.giveUpId = _target;
			}
		}

		const bool sneakOut = f.playerSneaking && !f.playerInCombat;
		// ROUND3 fix 4: stale verdicts no longer close the far tiers as a whole; Classify refuses only the foes that do not
		// fight the group (and every first strike). Rollback brain.staleFightOpen = 0.
		// Never a push since the load (SAFETY9: the deny set is empty then) still closes every far tier.
		const bool staleGate = stale && (!_everVerdict || p.staleFightOpen < 0.5);
		cx.farWhy = cx.range < 1 ? "range" : f.waiting ? "waiting" : staleGate ? "stale" : sneakOut ? "sneaking" : cx.busy ? "busy" :
		            f.playerMounted ? "mounted" : _chainLock ? "chainRest" : "";
		cx.farOpen = cx.farWhy[0] == '\0';
		cx.farHard = cx.range < 1 || f.waiting || f.playerMounted;  // HOTFIX round5C
		cx.meChest = f.meChest;
		cx.preemptOpen = p.requireCombat < 0.5 && (!stale || cx.staleNative) && !f.playerSneaking && !cx.busy;
		// ROUND5 (in game round 4 R2, museum: the raider behind the walls was "hidden" 2 s while he hovered by the player;
		// the user wants the whole building cleared): in a real interior cell with the navmesh on, a hostile of the same
		// cell is a first-strike target while hidden too (token, sneaking, busy, stale and every hard veto still apply).
		// brain.interiorHunt 0: round 4 (idle hostiles only in sight).
		cx.interiorHunt = p.interiorHunt >= 0.5 && f.interiorCell && p.nav >= 0.5 && w.NavAvailable();
		// FINISH A2 fix lane 2 r2 (brain.fpHereR): nobody attacks the player now - no candidate attacking it and none that hit it
		// within hitMemory s (Classify lets a seen far first strike near him through only then)
		cx.fpHereOn = p.fpHereR > 0 && f.interiorCell;  // (indoors: outdoors a later far trip is an open flight, no shut way)
		cx.plQuiet = cx.fpHereOn;
		for (const Cand& c : f.cands) {
			if (!cx.plQuiet) break;
			if (c.attackingPlayer || (c.lastHitPlayerAt > -1e8 && now >= c.lastHitPlayerAt - kTiny && now - c.lastHitPlayerAt <= p.hitMemory + kTiny)) cx.plQuiet = false;
		}

		// classify every kept candidate
		const auto& cs = f.cands;
		const int   n = static_cast<int>(cs.size());
		_info.assign(cs.size(), Info{});
		int  cur = -1;
		bool anyLocal = false;
		for (int i = 0; i < n; ++i) {
			const Cand& c = cs[i];
			Mem&        m = M(c.id);
			if (m.firstAt < -1e8) {
				m.firstAt = now;  // s12 fix r2 lane 2 (brain.freshHoldR)
				// completion side lane 4 s13r2 (brain.newcomerKnown; REVIEW_r3 3): "newcomer" (burstBigPassive / R7 bigFirstPick: a
				// candidate for <= kBigFirstHold s) was counted from this MEMORY, and the memory is lost by every Reset (load game, brain
				// off -> on at each soak segment move) and by Prune (30 s not a candidate). GK3 dt2 (20261023-205553, 96 s after the last
				// brain on): the passive Swan 00056B7B came back into the candidate list in a 6-8 raider burst, counted as new, was his
				// target 21:02:39.49-40.92 (keepThreat, ~kBigFirstHold) while the raiders shot the player. Now a candidate that the brain
				// knew before (pruned this life) or that was already there at the first Update after a Reset is OLD (a finite stamp far
				// back: no newcomer, no fresh hold); it still counts by its run, its aim or its fight. One that really turns up (the Concord
				// deathclaw out of its manhole, PR-14: 0.3 s after the brain came on) is a newcomer as before.
				if (p.newcomerKnown >= 0.5 && ((_resetAt > -1e8 && now <= _resetAt + kTiny) || _knownIds.count(c.id) != 0)) {
					// completion side 3 s13r3 (item 8 hole 2 again; the s13r3 integrator turned the key off for soak K1 rr 01:36:34.9-38.9,
					// "Swan came back hostile stamped old, taken 4 s late"): Swan was NOT stamped there (brain on 01:35:08.08, Swan's last
					// candidate frame 01:32:23 in the brain life before, newcomerOld 1 -> 1 from its entry to its pick; the key-0 soaks took
					// the turned Swan 3.0 / 3.1 s late too - the delay is R5's bigAtkMeR / the thrower rule's range, not this stamp).
					// The stamp is for a PASSIVE monster only (GK3 dt2): a known candidate that is back FIGHTING the group (its combat
					// target the player / him / a teammate, or attacking the player) is a newcomer as with the key 0 - every rule reads
					// such a foe by its aim / its fight before its age anyway, so this keeps the key 1 brain = the key 0 brain for every
					// monster that turned (desktop NK-03 / NK-04) and the log names who was stamped (s13c8 oldBigLast).
					// And the stamp acts only IN A BURST (Mem::knownOld, read by burstBigPass and View::age while _burst): the hole it closes is
					// the passive monster pulling him out of a raider ambush (GK3 dt2: 6-8 raiders); outside a burst a quiet big monster that
					// was there at a Runner restart is R7's newcomer as before (the Concord deathclaw out of its manhole when the brain comes
					// on - r3m 10-25 02:13:25.039 'brain on', its first scan line 35 ms later; desktop s81 PR-NK1)
					if (c.attackingPlayer || c.combatWithGroup) {
						++_newcomerFightN;
					} else {
						m.knownOld = true;
						++_newcomerOldN;
						if (c.cls == Cls::kBig) {
							++_newcomerOldBigN;
							_newcomerOldBigLast = c.id;
						}
					}
				}
			}
			// completion side lane 4 s13r2 (brain.preemptAimKeep): the player aims at it with a drawn weapon (its chest within
			// max(preemptAimR, preemptAimDeg) of the view ray, within preemptAimMaxD u) - the aim memory AimKept reads
			if (p.preemptAimKeep > 0 && f.playerDrawn) {
				const Vec3   av = Normalize(f.playerView, { 0, 1, 0 });
				const Vec3   ad = c.chest - f.playerEye;
				const double along = Dot(ad, av);
				if (along > 0 && along <= p.preemptAimMaxD + kTiny) {
					const double perp = Length(ad - av * along);
					if (perp <= std::max(p.preemptAimR, along * std::tan(std::clamp(p.preemptAimDeg, 0.0, 30.0) * sp::math::kDeg2Rad)) + kTiny) m.plAimAt = now;
				}
			}
			// s12 r3 lane 4 (R7b, brain.protectBigOnce): its last two DISTINCT hits on the player (one shot noted by several threads /
			// a pellet spread within kHitSep s is one hit; a clock that went back starts the memory over). Read only by R7b
			if (const double hp = c.lastHitPlayerAt; hp > -1e8 && std::isfinite(hp)) {
				constexpr double kHitSep = 0.15;
				if (hp < m.plHit[0] - kTiny) {
					m.plHit[0] = hp;
					m.plHit[1] = -1e9;
				} else if (hp > m.plHit[0] + kHitSep || m.plHit[0] < -1e8) {
					m.plHit[1] = m.plHit[0];
					m.plHit[0] = hp;
				}
			}
			m.seenAt = now;
			m.essOnly = c.essentialOnly;  // explore cycle m1 fix lane 1 (brain.spentHurtT)
			if (c.combatWithGroup) m.engagedAt = now;
			// s12 fix round 3 lane 2 (brain.essDownHold): an essential he knocked down that shoots the player again after the knock-down
			// (kEssDownUpHit s: not the shot that was already flying) is up - its hold ends now (a later punch's own spent stays)
			const bool essDownNow = m.essDownUntil > now;
			if (essDownNow && c.lastHitPlayerAt > m.essDownAt + kEssDownUpHit) {
				if (m.spentUntil <= m.essDownUntil + kTiny) m.spentUntil = std::min(m.spentUntil, now);
				m.essDownUntil = now;
				++_st.essDownUps;
			}
			// explore m1 lane 2 (brain.protectBigTurnMeR): the first frame of its current run of attacking the player (a gap over kAtkRunGap s,
			// a clock that went back or a new memory start a new run: the turn)
			if (constexpr double kAtkRunGap = 1.0; c.attackingPlayer && !(m.atkPlayerAt > -1e8 && now >= m.atkPlayerAt - kTiny && now - m.atkPlayerAt <= kAtkRunGap + kTiny))
				m.atkSince = now;
			if (c.attackingPlayer) m.atkPlayerAt = now;  // s12 fix r3 lane 3 (brain.atkFlipKeep)
			if (c.turned) m.turnedAt = now;              // FINISH A2 fix lane 5 (brain.turnedGuardMem: his knock-down hides the turned flag)
			// FINISH A2 lane 3 (brain.bigTurnRun): the first frame of its current run of FIGHTING THE GROUP (its combat target the player /
			// him / a teammate, or attacking the player) - a gap over 1 s, a clock that went back or a new memory start a new run: the
			// TURN. Lane 6's group-turn rule counted "just came" from Mem::firstAt (the first frame it was a CANDIDATE), but Swan stands in
			// its pond as a candidate for minutes before it wakes up, so its turn never counted (finA gate soaks 120152 / 124632, the
			// integrator's 105006 / 110400 / 112056: 'turned=true ck=2' 3.8-6.8 s after it became a candidate, taken 0.9-9.1 s later)
			if (const bool grpNow = c.combatWithGroup || c.attackingPlayer; grpNow) {
				if (!(m.grpAt > -1e8 && now >= m.grpAt - kTiny && now - m.grpAt <= 1.0 + kTiny)) m.grpSince = now;
				m.grpAt = now;
			}
			// FINISH B2 fix lane 5 (brain.recallKeepPlAtkR): the last Recall's OWN ignore on a foe that now attacks the player within the
			// radius is lifted (back to what the list said before the recall) - the foe came closer during the window; never a give-up made
			// since (Mem::giveUpAt), never the dropped target the keep would not keep (after his own put-back / Come here)
			if (p.recallKeepPlAtkR > 0 && m.recallIgnAt > -1e8 && m.ignoreUntil > now) {
				const bool own = m.recallIgnAt >= _recallAt - kTiny && now - _recallAt < p.recallIgnore + kTiny && m.giveUpAt < m.recallIgnAt - kTiny;
				if (!own) m.recallIgnAt = -1e9;
				else if (c.attackingPlayer && c.hostile && (c.id != _recallDropId || _recallDropOk) && GuardDist(c.feet, f.playerFeet) <= p.recallKeepPlAtkR + kTiny) {
					m.ignoreUntil = m.preRecallIgn;
					m.recallIgnAt = -1e9;
					++_st.recallPlAtkLifts;
				}
			}
			Classify(c, cx, m, _info[i]);
			if (m.essDownUntil > now) {  // (the label for the logs: 'essDown', not 'spent')
				++_st.essDownHolds;
				if (std::string_view(m.why ? m.why : "") == "spent") m.why = "essDown";
			}
			// completion lane 3 (brain.teleShutHold, Tele 1): a hidden foe that is not hurting the player, behind a way found shut (itself,
			// or a shut foe within teleShutR seen from within teleShutFrom of where that way failed), is not picked meanwhile (like an
			// ignored one; logs 'teleShut'): he goes on with the others or comes back to the player instead of re-running the stalled
			// leg. Seen, or hurting the player (the shield case), or at the player: picked as before at once.
			if (_info[i].pickable && m.vis != 1 && TeleNoPressCase(c, f, p) && TeleShutNow(c, f, p)) {
				_info[i].pickable = false;
				m.why = "teleShut";
				++_st.teleShutSkips;  // (frames x foes)
			}
			// DPREP-T (G7; live 21:38:32.74-33.0: 'brain give up 0011ADFB leash' 21x in 0.3 s, 21:11:47 the same with ghouls):
			// the leash was checked only on the foe Pick returned and excluded it for that frame, so it was picked and
			// dropped every frame. leashPick 1: every candidate is leashed BEFORE the pick (Pick never returns one, so no
			// hunt starts for it), a leashed foe stays leashed until he is back inside reach + leashExtra - leashHyst, and
			// the give-up is logged on the edge only. A leashed local foe still blocks far picks (rule zero, K11 recall).
			if (p.leashPick >= 0.5 && _info[i].pickable && _info[i].tier != Tier::kNone) {
				Info&        in = _info[i];
				const bool   towardCentre = p.leashToCentre >= 0.5 && in.dCentre <= p.guardStill && in.dCentre + p.leashExtra < cx.dMeCentre;
				const double lim = in.reach + p.leashExtra - (m.leashOn ? std::max(0.0, p.leashHyst) : 0.0);
				bool         leash = !in.pin && !towardCentre && cx.dMeCentre > lim;
				// s11 fix lane 2 (brain.farHuntPlayerR): a foe aiming at the player within farHuntPlayerR of it, nearer to it than he
				// is, is never leashed (it is where he must go back to anyway)
				if (leash && NearPlayerFoe(p, c, in) && in.dCentre + p.leashExtra < cx.dMeCentre) {
					leash = false;
					++_st.farHuntLeashKeeps;
				}
				if (leash) {
					in.excluded = true;
					in.leashed = true;
					m.why = "leash";
					if (!m.leashOn) {
						++_st.leashDrops;
						++_st.leashEdges;
						if (out.giveUp == GiveUp::kNone || c.id == _target) {
							out.giveUp = GiveUp::kLeash;
							out.giveUpId = c.id;
						}
					}
				}
				m.leashOn = leash;
			}
			if (c.id == _target) cur = i;
			anyLocal = anyLocal || (_info[i].pickable && IsLocal(_info[i].tier));
			if (stale && _info[i].pickable && IsFar(_info[i].tier)) ++_st.staleKeeps;  // ROUND3 fix 4 (frames x foes)
			if (cx.staleNative && _info[i].pickable && (_info[i].tier == Tier::kFarP || _info[i].tier == Tier::kPreempt)) ++_st.staleNatives;
		}
		// s11 fix lane 2 (brain.farHuntPlayerR; s11 gate GK1-rr dt1 08:10:26-40.8, the player beaten to death by the soak raider
		// FF001280 while he hunted street raiders 4000-6000 u from the player: the D2 defend break and the no-idle pick took the
		// SEEN street raider nearest to HIM over the hidden one closing on the player, twice): while a foe aiming at the player is
		// within farHuntPlayerR of it, a far hunt beyond farHuntPlayerR of the player (F2 / F3 / FP - never F1, it hit the player)
		// is held back: not picked, not by the no-idle pick, not by the defend break; the far commitment to it ends.
		if (p.farHuntPlayerR > 0) {
			bool near = false, nearAim = false;
			for (int i = 0; i < n && !nearAim; ++i) nearAim = _info[i].pickable && NearPlayerFoe(p, cs[i], _info[i]);
			near = nearAim;
			// s12 fix lane 3 (brain.farHuntGroup): a foe fighting the group near the player holds the far hunts back too (the burst
			// is finished before a street raider 4000+ u away; counted when it alone held one back: frames x foes). A foe he gave up a
			// moment ago (ignored, not spent: the no-idle pick still takes it) counts as well (desktop PR-13 seed 5: a burst raider
			// hidden behind a car 1500 u from the player, given up 'unreach', and he flew 5000 u to the street raiders for 2.9 s)
			const bool grp = p.farHuntGroup >= 0.5;
			const auto grpGaveUp = [&](const Info& a_in) { return grp && a_in.m && !a_in.essOut && a_in.m->spentUntil <= now && (a_in.m->ignoreUntil > now || a_in.undoneHold); };
			for (int i = 0; i < n && !near; ++i) near = (_info[i].pickable || grpGaveUp(_info[i])) && NearGroupFoe(p, cs[i], _info[i], now);
			if (near) {
				for (int i = 0; i < n; ++i) {
					Info& in = _info[i];
					// (s12 fix lane 3: with farHuntGroup a far one he gave up is held back too - else the no-idle pick, which takes
					// given-up foes, flies him to it)
					if (!(in.pickable || grpGaveUp(in)) || in.dPlayer <= p.farHuntPlayerR + kTiny || !(in.tier == Tier::kFar2 || in.tier == Tier::kFar3 || in.tier == Tier::kFarP)) continue;
					if (in.pin || now - cs[i].lastHitPlayerAt <= p.hitMemory + kTiny) continue;  // (a foe that hit the player is F1 anyway)
					// s12 fix round 2 lane 5 (brain.farHuntGroupAimR; REVIEW_r1 finding 1, r1 hf 034420 03:47:50.728 'dw=farHunt:1027'
					// with every far hold of that second from the group rule alone): when only foes fighting HIM / the group are near
					// (no aim at the player), a far foe whose combat target is the player (F2: a sniper, a missile raider) is no
					// lesser threat than they are - it is held back only beyond farHuntGroupAimR of the player (the motivating
					// street raider was 5201 u out). The near-aim hold (nearAim) is unchanged.
					if (!nearAim && p.farHuntGroupAimR > 0 && (cs[i].attackingPlayer || cs[i].combatWithPlayer) && in.dPlayer <= p.farHuntGroupAimR + kTiny) {
						if (in.pickable) ++_st.farGroupAimKeeps;
						continue;
					}
					in.excluded = true;
					in.farOut = true;
					if (!in.pickable) continue;  // (a given-up one keeps its log reason and is not counted)
					in.m->why = "farHunt";
					++_st.farHuntOuts;
					if (!nearAim) ++_st.farHuntGroupOuts;  // s12 fix lane 3
				}
			}
		}
		// s12 fix r2 lane 2 (brain.freshHoldR; s12r1 gate GQ hf 120 054846 CZ.b 05:52:27.1-29.9: right after CZ.a's last kill he went
		// for the Combat Zone's own world raiders 00111F4C / 00108B20 (F2, 3007-3822 u) while the Runner placed CZ.b's 5 raiders round
		// the player; they show up as candidates that are not eligible yet - hostile, in the local ring, the Papyrus token pending
		// ('FF001124:token' at 29.76) - so neither farHuntPlayerR nor farHuntGroup saw them): while such a FRESH hostile (first a
		// candidate within freshHoldT s, not eligible yet, token pending or asked) is within freshHoldR of the player, a far hunt
		// beyond farHuntPlayerR of the player (F2 / F3 / FP; never one that hit the player or a pinner) is held back exactly like the
		// farHuntPlayerR hold - he stays by the player for the burst that is about to start. A separate pass (the farHuntPlayerR
		// pass above is unchanged).
		if (p.freshHoldR > 0) {
			bool fresh = false;
			for (int i = 0; i < n && !fresh; ++i) {
				const Info& in = _info[i];
				const Cand& c = cs[i];
				fresh = in.tier == Tier::kNone && (in.pending || in.preVet) && !in.essOut && in.m && c.hostile && c.loaded && c.sameSpace && !c.killPending &&
				        in.dPlayer <= p.freshHoldR + kTiny && now - in.m->firstAt <= p.freshHoldT + kTiny;
			}
			if (fresh) {
				const double holdR = p.farHuntPlayerR > 0 ? p.farHuntPlayerR : p.freshHoldR;
				for (int i = 0; i < n; ++i) {
					Info& in = _info[i];
					if (!in.pickable || in.excluded || in.dPlayer <= holdR + kTiny || !(in.tier == Tier::kFar2 || in.tier == Tier::kFar3 || in.tier == Tier::kFarP)) continue;
					if (in.pin || now - cs[i].lastHitPlayerAt <= p.hitMemory + kTiny) continue;
					in.excluded = true;
					in.farOut = true;
					in.m->why = "farHunt";
					++_st.freshHoldOuts;
				}
			}
		}
		// HOTFIX round5C C (live round5C: 'spared(essential/protected)=true' on 00002F09 / 00002F1F / 000199D5 every ~3 s while
		// killable foes shot the player): an essential foe he knocked down rests essRest s whenever a killable hostile is
		// around, and is never picked after essMaxPerMin knock-downs within 60 s. With only essentials left he keeps them
		// down (within the cap); the runtime flings them farther (punch.essFlingMult). Resting ones never block a far hunt.
		{
			_st.defendCands = 0;
			bool killable = false;
			bool killableAtk = false;  // R6 step 8.1f integrator (brain.essRestAtk): a killable foe attacking THE PLAYER
			bool killableAtkFar = false;  // s12 fix round 2 lane 5: ... one beyond essRestNearR of the player (no hit), not counted
			const double essNearR = p.essRestNearR;  // s12 fix round 2 lane 5 (0 = s12r1)
			for (int i = 0; i < n; ++i) {
				const Cand& c = cs[i];
				const Info& in = _info[i];
				// (s12 fix round 3 lane 2, brain.essDownHold: an essential he just knocked down is down, not attacking - the engine's
				// combat target lags its bleedout flag; it is no 'attacker up' for the 1 Hz idle-under-fire sample either)
				if (c.attackingPlayer && !(in.m && in.m->essDownUntil > now)) ++_st.defendCands;
				if (in.denyOver && in.tier != Tier::kNone) ++_st.denyOverrides;
				if (!c.essentialOnly && EssDowns(c.id, now) == 0 && !c.killPending && c.hostile && c.loaded && c.sameSpace && (in.tier != Tier::kNone || in.pending)) {
					killable = true;
					// s12 fix lane 1 (s11c / s11f / s12g gates, HF3 Combat Zone: 6 of 7 hf runs, 0.23 / 0.09 at s11c; s11f 124214
					// 12:45:32-34): the Combat Zone's own world raiders he provoked fight HIM (c.defend), never the player; counted
					// here they kept the essential that got up and shot the player resting 25 s while he far-hunted them. Only a
					// killable foe that attacks THE PLAYER is a reason to leave a player-attacking essential down.
					// s12 fix round 2 lane 5 (brain.essRestNearR; s12r1 gate HF3 065443 06:58:03.168 'tgt=00108B20 dp=2
					// dw=essRest:2438', R1 034834 03:51:48.998-53.583 on 00111F4C / 001357C7 F2): the same world raiders later take the
					// PLAYER as combat target (dp 9-10) from 2260-3700 u out and never hit it (2 hits in 131 HF3 windows; the scene
					// gunners at 600-1100 u: 105) - one of them alone kept the essential resting while he flew to it. Only a killable
					// attacker within essRestNearR of the player, or one that hit it within hitMemory, is a reason now.
					if (c.attackingPlayer) {
						if (essNearR > 0 && in.dPlayer > essNearR + kTiny && now - c.lastHitPlayerAt > p.hitMemory + kTiny) killableAtkFar = true;
						else killableAtk = true;
					}
				}
			}
			// completion side lane 1 (brain.essHurtFirst, needs essRestAtk; the s13r1 hf run 092319 09:26:26.6-35.3: 'dw=essRest:345-555',
			// (only an essential he has knocked down before - the item's case; a never-downed one keeps the D2 order, killable attackers
			// first: s81 RULE-C.raider, the turned ex-ally and the raider that attacks with it)
			// an essential gunner that attacks the player rested while he hunted the arena's world raiders 1500-2200 u from the player -
			// one of them aimed at the player from within essRestNearR, so it counted as a killable attacker - and the user's rule of
			// Sat 02:40: the attacker CLOSEST to the player first): an essential that attacks the player and hit it again since its last
			// noted knock-down (within hitMemory; not the shot already flying, kEssDownUpHit; not while its down hold runs), within
			// essRestNearR of the player, is 'hurting near'. A killable attacker of the player (the killableAtk set) keeps it resting /
			// capped out only when that one hit the player within hitMemory too, or stands no farther from the player than it +
			// essHurtPad; Pick's D2 essential skip reads it the same way (Info::essHurt).
			const bool hurtFirst = p.essHurtFirst >= 0.5 && p.essRestAtk >= 0.5;
			const auto killAtkRest = [&](int a_k) {  // (the killableAtk test above, per foe)
				const Cand& c = cs[a_k];
				const Info& in = _info[a_k];
				return !c.essentialOnly && EssDowns(c.id, now) == 0 && !c.killPending && c.hostile && c.loaded && c.sameSpace && (in.tier != Tier::kNone || in.pending) &&
				       c.attackingPlayer && !(essNearR > 0 && in.dPlayer > essNearR + kTiny && now - c.lastHitPlayerAt > p.hitMemory + kTiny);
			};
			const auto restOutranked = [&](int a_e) {
				for (int k = 0; k < n; ++k)
					if (k != a_e && killAtkRest(k) &&
						(now - cs[k].lastHitPlayerAt <= p.hitMemory + kTiny || _info[k].dPlayer <= _info[a_e].dPlayer + std::max(0.0, p.essHurtPad) + kTiny))
						return true;
				return false;
			};
			if (hurtFirst) {
				for (int i = 0; i < n; ++i) {
					const Cand&  c = cs[i];
					Info&        in = _info[i];
					const double hp = c.lastHitPlayerAt;
					in.essHurt = c.essentialOnly && c.attackingPlayer && c.hostile && !c.killPending && c.loaded && c.sameSpace && in.m && std::isfinite(hp) && hp > -1e8 &&
					             now >= hp - kTiny && now - hp <= p.hitMemory + kTiny && in.m->essDownAt > -1e8 && hp > in.m->essDownAt + kEssDownUpHit && !(in.m->essDownUntil > now) &&
					             !(essNearR > 0 && in.dPlayer > essNearR + kTiny);
					// explore m1 fix lane 3 (brain.essAimFirst; s13cx gate hf 1080u 20261025-060858 06:11:51-56: the essential FF0010EA, never
					// knocked down, attacked the player from 6.9 s into HF3 and shot it at 53.45 / 53.71 while he chased the hidden killable
					// raiders FF0010A2 / E3 / E0 / E6 1000-1500 u off (D2 skip: 'never an essential while a killable attacker of the player
					// exists'; this rule only knew essentials he had knocked down before); picked 2.8 s after its first hit, HF3 0.48. xp_m1d CZ
					// cage 10:10:52-54: the essential FF001333 hit the player 3x, never picked while 4 killables lived): an essential he has not
					// knocked down within 60 s that attacks the player near it and hit it within hitMemory, or that he SEES, or that stands
					// within protectNearR of it, is 'hurting near' too (essAim: Pick's D2 skip lets only a killable attacker that hit the player,
					// that he sees or that stands within protectNearR of it keep it out; the reverse break takes it off a hidden killable target)
					if (!in.essHurt && p.essAimFirst >= 0.5 && c.essentialOnly && c.attackingPlayer && c.hostile && !c.killPending && c.loaded && c.sameSpace && in.m &&
					    !(in.m->essDownUntil > now) && EssDowns(c.id, now) == 0 && !(essNearR > 0 && in.dPlayer > essNearR + kTiny)) {
						const bool hitRecent = std::isfinite(hp) && hp > -1e8 && now >= hp - kTiny && now - hp <= p.hitMemory + kTiny;
						if (hitRecent || in.m->vis == 1 || in.dPlayer <= p.protectNearR + kTiny) {
							in.essHurt = in.essAim = true;
							++_st.m1EssAim;
						}
					}
				}
			}
			// s12 r3 fix lane 1 (brain.burstDp): a burst = at least burstDp candidates attacking the player this Update
			_burst = p.burstDp > 0 && _st.defendCands >= static_cast<int>(p.burstDp + 0.5);
			if (_burst) ++_st.burstFrames;
			// FINISH A2 lane 3 (brain.bigTurnRun): somebody hurt the player within s2fHitWin s this Update (TurnGroupBig's turn path waits then)
			_plHurtNow = false;
			// round 2: the two attackers of the player nearest to HIM (pickable, alive; chest to chest) - the turn path waits for a nearer one
			_atkMe1Id = 0;
			_atkMe1 = _atkMe2 = 1e18;
			_atkNearPl = false;  // finB (REVIEW 2): an attacker of the player close to it or running at it
			if (p.bigTurnRun >= 0.5)
				for (int i = 0; i < n; ++i) {
					const Cand&  c = cs[i];
					const double hp = c.lastHitPlayerAt;
					if (!_plHurtNow)
						_plHurtNow = c.hostile && c.loaded && c.sameSpace && !c.killPending && std::isfinite(hp) && hp > -1e8 && now >= hp - kTiny && now - hp <= p.s2fHitWin + kTiny;
					const Info& in = _info[i];
					if (!c.attackingPlayer || !c.hostile || !c.loaded || !c.sameSpace || c.killPending || !in.pickable || in.excluded || in.tier == Tier::kNone) continue;
					if (!_atkNearPl) {
						const double dPl = Length(Flat(c.feet - f.playerFeet));
						const double runAt = Dot(Flat(c.vel), FlatDir(f.playerFeet - c.feet));
						const double grpR = p.bigTurnGroupR > 0 ? p.bigTurnGroupR : p.bigAimR;
						_atkNearPl = std::isfinite(dPl) && dPl <= grpR + kTiny && runAt >= sp::protect::kRushMinClose &&
						             runAt <= sp::protect::kRushMaxClose;
					}
					const double dm = Length(c.chest - f.meChest);
					if (!std::isfinite(dm)) continue;
					if (dm < _atkMe1) {
						_atkMe2 = _atkMe1;
						_atkMe1 = dm;
						_atkMe1Id = c.id;
					} else if (dm < _atkMe2) {
						_atkMe2 = dm;
					}
				}
			// FINISH lane 6 A_big_burst (brain.burstLeashR; finish\K07_ANALYSIS.md rows 2 / 3: soak 20261022-003129 dt3 00:42:40-46.4 - after
			// each kill the tier-E chain took FF001A1E 1336 u, FF0019DE 1649 / 1861 u from him while FF001A14 shot the player 0.84 -> 0.31;
			// 20261024-094522 dt2 09:52:00-05 the same shape, 20261023-164951-rr dt2 16:56:32 the hurter rule flew him 1834 u out): the
			// BURST LEASH. While at least burstLeashN hostiles (loaded, in his space, not kill-pending) stand within burstLeashNearR of the
			// player, a candidate that does NOT attack the player (tier E / P: it fights him or nobody) beyond burstLeashNearR of the player
			// is held back exactly like the farHuntPlayerR hold (not picked, not by the no-idle pick, no guard view), and one beyond
			// burstLeashR while a pickable candidate within burstLeashR is up. Attackers of the player are never held: the burst cost
			// (brain.burstDp: distance to the player) and the closest-attacker rules order those - holding them back cost the desktop
			// BURST DT3F x3 (gunners 700-2000 u from the player, him 1700 u away) 9 god-backs of 76. Never either: one that hit the player
			// within protectHurtWin, a pinner, a big monster or an essential (their own rules), nor F1 (it hit the player). He always
			// keeps a target: each hold needs a pickable candidate inside the radius it enforces. 0 = off (bit exact).
			_burstNear = 0;
			if (p.burstLeashR > 0) {
				const double nearR = std::max(p.burstLeashNearR, p.burstLeashR);
				for (int i = 0; i < n; ++i) {
					const Cand& c = cs[i];
					if (c.hostile && c.loaded && c.sameSpace && !c.killPending && _info[i].dPlayer <= nearR + kTiny) ++_burstNear;
				}
				if (_burstNear >= static_cast<int>(p.burstLeashN + 0.5)) {
					bool anyIn = false, anyNear = false;
					for (int i = 0; i < n; ++i) {
						const Info& in = _info[i];
						if (!in.pickable || in.excluded || in.tier == Tier::kNone) continue;
						if (in.dPlayer <= nearR + kTiny) anyNear = true;
						if (in.dPlayer <= p.burstLeashR + kTiny) anyIn = true;
					}
					for (int i = 0; i < n; ++i) {
						Info&       in = _info[i];
						const Cand& c = cs[i];
						if (!in.pickable || in.excluded || !in.m || in.tier == Tier::kNone || in.tier == Tier::kFar1 || in.pin) continue;
						if (c.attackingPlayer || c.cls == Cls::kBig || c.essentialOnly) continue;
						if (now >= c.lastHitPlayerAt - kTiny && now - c.lastHitPlayerAt <= p.protectHurtWin + kTiny) continue;
						bool held = false;
						if (in.dPlayer > nearR + kTiny) held = anyNear;
						else if (in.dPlayer > p.burstLeashR + kTiny) held = anyIn;
						if (!held) continue;
						in.excluded = true;
						in.farOut = true;
						in.m->why = "burstLeash";
						++_st.burstLeashOuts;
					}
				}
			}
			_essHoldIds.clear();
			if (p.essRest > 0 || p.essMaxPerMin > 0) {
				for (int i = 0; i < n; ++i) {
					Info& in = _info[i];
					if (!in.m) continue;
					const int downs = EssDowns(cs[i].id, now);
					if (downs <= 0) continue;
					const double last = *std::max_element(std::begin(in.m->essAt), std::end(in.m->essAt));
					const bool   cap = p.essMaxPerMin > 0 && downs >= static_cast<int>(p.essMaxPerMin);
					// R6 step 8.1f integrator (brain.essRestAtk; HF3 21:56:00.57-01.48: the resting essential FF0010DD shot the player
					// 1.0 -> 0.30 in 3 hits while he chained far hunts of raiders that attacked nobody): an essential attacking the
					// player / the group rests only while a killable foe that ATTACKS the player / the group is around (those come
					// first, the round-5-C reason for the rest); otherwise it is knocked down again (the 2-a-minute cap holds)
					// (s12 fix lane 1: an essential that attacks the PLAYER; one that fights only him / a teammate rests as round 5 C)
					const bool   atkPl = p.essRestAtk >= 0.5 && cs[i].attackingPlayer;
					bool         restBy = atkPl ? killableAtk : killable;
					// completion side lane 1 (brain.essHurtFirst): a hurting-near essential rests / is capped out only behind a killable
					// attacker of the player that outranks it (hit the player too, or no farther from it); capped it is held down (essHold)
					if (hurtFirst && atkPl && restBy && in.essHurt && !restOutranked(i)) {
						restBy = false;
						++_st.essHurtWakes;  // (frames x essentials)
					}
					if (atkPl && !killableAtk && killableAtkFar) ++_st.essAtkFarSkips;  // s12 fix round 2 lane 5 (frames x essentials)
					const bool   rest = !cap && restBy && p.essRest > 0 && EssClock(now) - last <= p.essRest;  // D1H: the essential clock
					if (!cap && !rest) {
						if (killable && !restBy && p.essRest > 0 && EssClock(now) - last <= p.essRest) ++_st.essRestAtkEnds;  // (frames x foes)
						continue;
					}
					// HOTFIX2 (smoke F2, user rule C 03:35 "when only essentials remain, keep them down"): a capped essential that
					// attacks the group while no killable foe is around is not left alone (he idled 10 s while it shot the
					// player): he stays on it and the runtime gives it control hits only (fling, no damage: no new knock-down,
					// so the 2-a-minute cap holds). brain.essHold 0 = the round-5-C hotfix (capped = left alone).
					// s12 fix lane 1 (brain.essRestAtk 1; s8.1f HF3 22:56: both essentials capped within 8 s, then left alone while
					// the world raiders were "killable"): a capped essential attacking the PLAYER while no killable foe attacks the
					// player is held down as well (he stays on it; control flings only, the cap holds) instead of a far hunt.
					if (cap && (!killable || (atkPl && !restBy)) && p.essHold >= 0.5 && (cs[i].attackingPlayer || cs[i].defend)) {
						in.essHold = true;
						if (in.pickable) in.m->why = "essHold";
						_essHoldIds.push_back(cs[i].id);
						++_st.essHolds;
						continue;
					}
					in.pickable = in.pending = in.preVet = false;
					in.essOut = true;
					in.m->why = cap ? "essCap" : "essRest";
					if (cap) ++_st.essCaps;
					else ++_st.essRests;
				}
			}
			// s12 fix lane 1 (brain.essRestAtk 1; s12g gate HF3: F2 / F3 / FP picks of the Combat Zone's world raiders 00098087 /
			// 00108B20 / 00111F4B / 001357C7 ... while an essential shot the player, 6 of 7 hf runs): while an essential that attacks
			// the player is up (not resting / capped out: its rest ended, or it is held down), no far hunt (F2 / F3 / FP) of a foe
			// that neither attacks nor hit the player - he stays on the shooter. The farHuntPlayerR rule above skips essentials, so
			// this is its essential half (same flags: not picked, not by the no-idle pick, not by the defend break). Its 0.6 s
			// 'spent' after each fling counts as up too, or he would fly off to a far raider between two hold-down flings; one he
			// ignores after failed reaches does not (no idle under fire: the far hunts stay open then, as before).
			if (p.essRestAtk >= 0.5) {
				bool essUp = false, essUpNear = false;  // s12 fix round 2 lane 5: + one up within essRestNearR of the player
				for (int i = 0; i < n && !(essUp && (essUpNear || essNearR <= 0)); ++i) {
					const Cand& c = cs[i];
					const Info& in = _info[i];
					const bool  up = c.essentialOnly && c.attackingPlayer && c.hostile && !c.killPending && c.loaded && c.sameSpace && !in.essOut && in.tier != Tier::kNone &&
					                (in.pickable || (in.m && in.m->spentUntil > now));
					essUp = essUp || up;
					essUpNear = essUpNear || (up && essNearR > 0 && in.dPlayer <= essNearR + kTiny);
				}
				if (essUp) {
					for (int i = 0; i < n; ++i) {
						Info&       in = _info[i];
						const Cand& c = cs[i];
						if (!in.pickable || in.excluded || c.essentialOnly || !(in.tier == Tier::kFar2 || in.tier == Tier::kFar3 || in.tier == Tier::kFarP)) continue;
						if (in.pin || now - c.lastHitPlayerAt <= p.hitMemory + kTiny) continue;
						// s12 fix round 2 lane 5 (brain.essRestNearR; R1 034834 03:51:51.977: he left the essential FF0010EC, up and
						// shooting the player, for the F2 world raider 001357C7 3050 u from him, 3469 u from the player): a foe that
						// aims at the player is held back too while such an essential is up near the player, when it aims from
						// beyond essRestNearR of the player and has not hit it (the farHuntPlayerR near-aim rule skips essentials)
						if (c.attackingPlayer) {
							if (!(essUpNear && in.dPlayer > essNearR + kTiny)) continue;
							++_st.essUpAtkOuts;
						} else {
							++_st.essUpFarOuts;
						}
						in.excluded = true;
						in.farOut = true;
						in.m->why = "farHunt";
					}
				}
			}
			// s13 fix r1 lane 2 (brain.essHoldStay; s13g gate hf 041401 04:17:14.5-23.6: after each control hit on the held essential
			// FF0010F4 shooting the player he flew 1545-1606 u to the essential FF0010F2 that attacked nobody, 'dw=spent:137', and back):
			// while he stays on a held essential through its window, a foe that neither attacks nor hit the player (within hitMemory),
			// pins or is a big threat is no pick (not by the no-idle pick, not by the defend break); attackers of the player stay
			// pickable. The stay ends when the held essential is no longer held (a killable attacker of the player came: essCap out,
			// it rests; or it no longer attacks): then it is spent as before.
			if (p.essHoldStay >= 0.5) {
				for (int i = 0; i < n; ++i)
					if (_info[i].essStayShot) ++_st.essStayShots;
				if (cur >= 0 && _info[cur].essStay) {
					Info& ci = _info[cur];
					if (ci.essOut || !ci.essHold) {
						ci.essStay = false;
						if (ci.m) {
							ci.m->essStayUntil = now;
							if (ci.m->spentUntil > now) {
								ci.pickable = false;
								if (!ci.essOut) ci.m->why = "spent";
							}
						}
					} else {
						ci.m->why = "essStay";
						++_st.essStays;
						for (int i = 0; i < n; ++i) {
							Info&       in = _info[i];
							const Cand& c = cs[i];
							if (i == cur || !in.pickable || in.excluded || !in.m) continue;
							if (c.attackingPlayer || in.pin || (c.lastHitPlayerAt > -1e8 && now - c.lastHitPlayerAt <= p.hitMemory + kTiny)) continue;
							in.excluded = true;
							in.farOut = true;  // (the no-idle pick skips it too)
							in.m->why = "essStay";
							++_st.essStayOuts;
						}
					}
				}
			}
			// s12 fix round 2 lane 5: the s12r1 gate's HF3 item 5, measured on the brain's own facts (the 1 Hz 'essOutFar'): a
			// frame with an essential that attacks the player resting / capped out while his target is a non-essential foe
			// beyond essRestNearR (2000 u when 0) of the player that has not hit it within hitMemory
			if (cur >= 0 && !cs[cur].essentialOnly && _info[cur].dPlayer > (essNearR > 0 ? essNearR : 2000.0) + kTiny && now - cs[cur].lastHitPlayerAt > p.hitMemory + kTiny) {
				for (int i = 0; i < n; ++i)
					if (cs[i].essentialOnly && cs[i].attackingPlayer && _info[i].essOut) {
						++_st.essOutFar;
						break;
					}
			}
			anyLocal = false;
			for (int i = 0; i < n; ++i) anyLocal = anyLocal || (_info[i].pickable && IsLocal(_info[i].tier));
		}
		// the far target ended (killed / gone): the chain bonus window opens (HUNT §2.3)
		if (_target && _tFar && (cur < 0 || cs[cur].killPending)) {
			_lastFarEndAt = now;
			_lastFarTier = _tTier;
		}

		// the current target's sight first (multi-ray), so the score sees this frame's occlusion, not last frame's
		bool curTraced = false, curVis = false;
		if (cur >= 0 && _info[cur].pickable) {
			curVis = TargetSight(cs[cur], *_info[cur].m, f, p, w, 3);
			curTraced = true;
		}
		// s12 r2 fix lane 1 (brain.protNearR, Pick rule zero): the current target is an essential / protected foe fighting the group
		// within protNearR of the player with a clear line from its head to the player's chest (one ray every kPlLosEvery s while
		// it qualifies; the answer is trusted for 1 s) - the score keeps him on it (s12r1 K4 cz1 05:11:34.26)
		if (p.protNearR > 0 && cur >= 0) {
			const Cand& c = cs[cur];
			Info&       in = _info[cur];
			if (in.pickable && !in.excluded && in.m && IsLocal(in.tier) && c.essential && c.combatWithGroup && c.hostile && !c.killPending &&
				in.dPlayer <= p.protNearR + kTiny) {
				Mem& m = *in.m;
				if ((now < m.plLosAt || now - m.plLosAt >= kPlLosEvery - kTiny) && CanCast(1)) {
					// (from the player's chest - World::Clear passes the player and him - to 60 u short of its head, so its own capsule
					// is never the hit; within 120 u it is right at the player)
					const Vec3   head = HeadOf(c);
					const Vec3   d = head - f.playerChest;
					const double l = Length(d);
					m.plLos = !(l > 120.0) || RClear(w, f.playerChest, f.playerChest + d * ((l - 60.0) / l));
					m.plLosAt = now;
					++_st.protNearRays;
				}
				_protNearCur = m.plLos && now >= m.plLosAt && now - m.plLosAt <= 1.0 + kTiny;
			}
		}
		// s12 r3 fix lane 1 (brain.burstLosPen, Pick rule zero's burst score): in a burst, the clear line from a foe attacking the player
		// to the player's chest - ONE ray a frame (budgeted, World::Clear: the existing SEH-shielded brain ray), round robin over the
		// candidates attacking the player within kBurstLosR of the player, each at most every kBurstLosEvery s; the answer is trusted
		// for 1 s (the same Mem field and ray as protNearR's)
		if (_burst && p.burstLosPen > 0 && n > 0) {
			for (int k = 0; k < n; ++k) {
				_burstLosRr = (_burstLosRr + 1) % n;
				const Cand& c = cs[_burstLosRr];
				Info&       in = _info[_burstLosRr];
				if (!in.m || !c.attackingPlayer || !c.hostile || c.killPending || in.dPlayer > kBurstLosR) continue;
				Mem& m = *in.m;
				if (now >= m.plLosAt && now - m.plLosAt < kBurstLosEvery - kTiny) continue;
				if (!CanCast(1)) break;
				const Vec3   head = HeadOf(c);
				const Vec3   d = head - f.playerChest;
				const double l = Length(d);
				m.plLos = !(l > 120.0) || RClear(w, f.playerChest, f.playerChest + d * ((l - 60.0) / l));
				m.plLosAt = now;
				++_st.burstLosRays;
				break;
			}
		}

		// ---- pick loop (L4D2 Update: up to 3 picks per frame, give-ups re-pick in the same Update)
		int        sel = -1;
		bool       vis = false;
		const bool navOn = p.nav >= 0.5 && w.NavAvailable();  // ROUND3 REACH: the stage chain owns hidden time
		// R6 step 4 (Facts::ext.on): the Warden picked; this loop runs once on its target and only moves him. Its give-ups are
		// reach failures (ExtReachFail: the target is kept, the reach chain starts over), never an ignore list.
		int extIdx = -1;
		if (f.ext.on) {
			++_st.extFrames;
			for (int k = 0; k < n && f.ext.id; ++k)
				if (cs[k].id == f.ext.id) {
					extIdx = k;
					break;
				}
			if (f.ext.id && extIdx < 0) ++_st.extMissing;
			// an actor the D2f rules never pick (a turned companion, a dormant foe the player shot, a resting essential that
			// attacks the player ...) still needs a tier for the old movement: from the Warden's band and its distance
			if (extIdx >= 0 && _info[extIdx].tier == Tier::kNone) {
				Info&        ei = _info[extIdx];
				const double dC = GuardDist(cs[extIdx].feet, f.centre);
				ei.tier = (f.ext.band >= 3) ? (dC <= p.attackerGuard ? Tier::kAttacker : Tier::kFar1)
				        : (f.ext.band == 2) ? (dC <= cx.R ? Tier::kEngaged : Tier::kFar3)
				                            : (dC <= p.rangedGuard ? Tier::kPreempt : Tier::kFarP);
				ei.reach = 1e18;
			}
		}
		// the Warden's target missing from the candidates (its scan pin was refused: kept list full, stale facts): the D2f pick
		// this frame - never idle under fire because of a hand-over (counted: extMissing)
		const bool ext = f.ext.on && (f.ext.id == 0 || extIdx >= 0);
		const bool extShield = ext && f.ext.shield;
		// R6 step 8.1 (brain.protectLock): the protect guard's config for this Update (core/protect.h)
		// (integrator, brain.protectNeedMask: only while his eyes see past bodies - with the crowd-blind legacy pick the guard
		// kept him hovering over hurters he could not see, TESTPLAN F1; the game's sight-mask crash shield turns it off too)
		_st.protMaskOff = p.protectLock >= 0.5 && p.protectNeedMask >= 0.5 && !f.eyesMasked;
		if (_st.protMaskOff) ++_st.protMaskOffFrames;
		_protCfg.on = p.protectLock >= 0.5 && !_st.protMaskOff;
		_protCfg.hurterFirst = static_cast<int>(std::clamp(p.protectHurterFirst, 0.0, 7.0) + 0.5);  // fix lane 2: bit 4 = R4t
		_protCfg.rushT = std::clamp(p.protectRushT, 0.0, 5.0);                                     // fix lane 2
		_protCfg.rushReach = std::clamp(p.protectRushReach, 0.0, 1000.0);
		_protCfg.bigAtkR = std::clamp(p.protectBigAtkR, 0.0, 8000.0);  // step 10 fix lane 1 (R5)
		_protCfg.closerR = std::clamp(p.protectCloserR, 0.0, 3000.0);
		_protCfg.closerFar = std::clamp(p.protectCloserFar, 0.0, 20000.0);
		_protCfg.closerAtR = std::clamp(p.protectCloserAtR, 0.0, 1000.0);  // explore m1 fix lane 3 (R5: a closer AT the player)
		_protCfg.nonFighter = std::clamp(p.protectNonFighter, 0.0, 1.0);  // s10f integrator (R5n)
		_protCfg.bomberR = std::clamp(p.protectBomberR, 0.0, 8000.0);     // s11 fix lane 1 (R6b)
		_protCfg.bomberFirst = std::clamp(p.protectBomberFirst, 0.0, 1.0);  // completion lane 1 (the thrower first)
		_protCfg.closerGuard = std::clamp(p.protectCloserGuard, 0.0, 1.0);  // completion lane 1 (R5's guards)
		_protCfg.bigAtkMeR = std::clamp(p.protectBigAtkMeR, 0.0, 20000.0);
		_protCfg.bomberGuard = std::clamp(p.protectBomberGuard, 0.0, 1.0);  // s13 r2 fix lane 4 (the thrower rule's guard)
		_protCfg.bomberMeR = std::clamp(p.protectBomberMeR, 0.0, 20000.0);
		_protCfg.rushMelee = std::clamp(p.protectRushMelee, 0.0, 1.0);        // completion side lane 4 (the rush rank: melee foes only)
		_protCfg.bomberFarR = std::clamp(p.protectBomberFarR, 0.0, 20000.0);  // completion side lane 4 (s13r2: renamed from bomberMeR) (no far thrower over a near attacker)
		_protCfg.bomberCurMeR = std::clamp(p.protectBomberCurMeR, 0.0, 20000.0);
		_protCfg.bomberCommit = std::clamp(p.protectBomberCommit, 0.0, 1.0);      // s13 r3 fix lane 1 (no thrower ping-pong)
		_protCfg.bomberCommitT = std::clamp(p.protectBomberCommitT, 0.0, 60.0);
		_protCfg.bomberFarNear = std::clamp(p.protectBomberFarNear, 0.0, 1.0);    // s13 r3 fix lane 1 (REVIEW_r2 1)
		_protCfg.bomberAimR = std::clamp(p.protectBomberAimR, 0.0, 20000.0);  // s13 r3 fix lane 2 (a thrower aiming at the player in its throw range)
		_protCfg.bomberKeep = std::clamp(p.protectBomberKeep, 0.0, 1.0);      // s13 r3 fix lane 2 (the current thrower over a farther hurter)
		_protCfg.bomberAimHurtT = std::clamp(p.protectBomberAimHurtT, 0.0, 60.0);  // completion side 4 s13r3 (no far thrower by the aim range while a nearer foe shoots the player)
		_protCfg.bomberAtkR = std::clamp(p.protectBomberAtkR, 0.0, 20000.0);      // explore m1 lane 2 (no thrower first over an attacker near the player)
		_protCfg.closerHidden = std::clamp(p.protectCloserHidden, 0.0, 1.0);      // explore m1 lane 2 (R5's closer over a hidden choice)
		_protCfg.bigTurnMeR = std::clamp(p.protectBigTurnMeR, 0.0, 20000.0);      // explore m1 lane 2 (a big monster that just turned on the player)
		_protCfg.bigTurnT = std::clamp(p.protectBigTurnT, 0.0, 30.0);
		_protCfg.rushGuard = static_cast<int>(std::clamp(p.protectRushGuard, 0.0, 15.0) + 0.5);  // completion side 2 s13r2 (bits: 1 no rush off a thrower, 2 R4t rushers only, 4 not off a near-player attacker; side 2 s13r3: 8 RC between equal rushers)
		_protCfg.first = static_cast<int>(std::clamp(p.protectFirst, 0.0, 3.0) + 0.5);  // explore cycle m1 fix lane 1 (bits: 1 the suicider, 2 the turned ally)
		_protCfg.suicideR = std::clamp(p.protectSuicideR, 0.0, 20000.0);
		_protCfg.firstNarrow = std::clamp(p.protectFirstNarrow, 0.0, 1.0);      // finish A fix lane 2 (from m2 lane 1; REVIEW m1 items 1 + 2)
		_protCfg.suicideNearR = std::clamp(p.protectSuicideNearR, 0.0, 20000.0);
		_protCfg.turnedNearR = std::clamp(p.protectTurnedNearR, 0.0, 20000.0);
		_protCfg.bombHurtFirst = std::clamp(p.protectBombHurtFirst, 0.0, 1.0);  // finish A fix lane 2 (from m2 lane 1; REVIEW m1 item 3)
		_protCfg.leaveHitT = std::clamp(p.protectLeaveHitT, 0.0, 10.0);  // explore cycle m2 fix lane 3 (the leave guard, LeaveHeld)
		_protCfg.leaveNearR = std::clamp(p.protectLeaveNearR, 0.0, 5000.0);
		_protCfg.leaveThrow = std::clamp(p.protectLeaveThrow, 0.0, 1.0);  // finish A fix lane 2
		_protCfg.bombHidFix = std::clamp(p.protectBombHidFix, 0.0, 1.0);
		_protCfg.bombDropKeep = std::clamp(p.protectBombDropKeep, 0.0, 1.0);
		// finish A2 (Fable, the Mass Fusion roof army): the thrower rule's stability - the swap's sight hysteresis, the close current thrower kept,
		// the hurting current thrower kept against a thrower that did not hit (all 0 = finA)
		_protCfg.bombSwapHid = std::clamp(p.protectBombSwapHid, 0.0, 10.0);
		_protCfg.bombCloseKeep = std::clamp(p.protectBombCloseKeep, 0.0, 5000.0);
		_protCfg.bombHurtKeep = std::clamp(p.protectBombHurtKeep, 0.0, 1.0);
		_protCfg.bomberFarAtk = std::clamp(p.protectBomberFarAtk, 0.0, 1.0);  // FINISH B2 fix lane 5 (the far thrower after the attacker at the player)
		_protCfg.bombFreshHit = std::clamp(p.protectBombFreshHit, 0.0, 1.0);
		_protCfg.passR = std::clamp(p.protectPassR, 0.0, 2000.0);
		_protCfg.passHold = std::clamp(p.protectPassHold, 0.0, 3.0);  // fix lane 1 (R0h)
		_protCfg.passPad = std::clamp(p.protectPassPad, 0.0, 2000.0);
		_protCfg.hurtWin = std::clamp(p.protectHurtWin, 0.0, 10.0);
		_protCfg.dwell = std::clamp(p.protectDwell, 0.0, 5.0);
		_protCfg.flipBack = std::clamp(p.protectFlipBack, 0.0, 10.0);
		_protCfg.nearR = std::clamp(p.protectNearR, 0.0, 5000.0);
		_protCfg.bigR = std::clamp(p.protectBigR, 0.0, 5000.0);
		_protCfg.closeKeepR = std::clamp(p.protectCloseKeepR, 0.0, 2000.0);
		_protCfg.hiddenPen = std::clamp(p.protectHiddenPen, 0.0, 20000.0);
		_protCfg.farHuntR = std::clamp(p.farHuntPlayerR, 0.0, 20000.0);  // s11 fix lane 2 (R3 yields to a pick near the player)
		_protCfg.essYield = std::clamp(p.protectEssYield, 0.0, 1.0);     // s11 fix integrator (R3 yields an essential to a killable)
		_protCfg.bigReturnT = std::clamp(p.protectBigReturnT, 0.0, 5.0);   // s12 fix lane 3 (BigReturn)
		_protCfg.bigFinishR = std::clamp(p.protectBigFinishR, 0.0, 2000.0);  // s12 fix lane 3 (BigFinish)
		_protCfg.bigFinishT = std::clamp(p.protectBigFinishT, 0.0, 3.0);
		_protCfg.bigAimR = std::clamp(p.bigAimR, 0.0, 8000.0);           // s12 fix lane 2 (ThreatRank 2: a big monster aiming at the player)
		_protCfg.bigAimMeR = std::clamp(p.bigAimMeR, 0.0, 20000.0);      // s12 fix lane 2 (... within this of him; 0 = no limit)
		_protCfg.bigFirst = std::clamp(p.protectBigFirst, 0.0, 1.0);     // s12 fix round 2 lane 3 (R7: the big monster before a lone hidden ranged hurter)
		_protCfg.bigFirstMeR = std::clamp(p.protectBigFirstMeR, 0.0, 20000.0);
		_protCfg.bigFirstHurterR = std::clamp(p.protectBigFirstHurterR, 0.0, 20000.0);
		_protCfg.bigFirstPick = std::clamp(p.protectBigFirstPick, 0.0, 1.0);  // s12 fix round 3 lane 2 (REVIEW_r2 2: no passive monster by the pick alone)
		_protCfg.commit = std::clamp(p.protectCommit, 0.0, 1.0);         // s12 r2 lane 4 (RC: the committed hurter)
		_protCfg.commitMem = std::clamp(p.protectCommitMem, 0.0, 30.0);
		_protCfg.commitAtPl = std::clamp(p.protectCommitAtPl, 0.0, 1.0);  // s12 r3 fix lane 1 (REVIEW_r2 1: CommitAtPlayer)
		_protCfg.burst = _burst;                                           // s12 r3 fix lane 1 (BurstRush / KeepA, this Update)
		_protCfg.burstLead = std::clamp(p.burstLead, 0.0, 5.0);
		_protCfg.burstKeepA = std::clamp(p.burstKeepA, 0.0, 10.0);
		_protCfg.bigOnce = std::clamp(p.protectBigOnce, 0.0, 1.0);       // s12 r3 lane 4 (R7b: the big monster before a one-shot raider)
		_protCfg.bigOnceR = std::clamp(p.protectBigOnceR, 0.0, 5000.0);
		_protCfg.bigOnceMem = std::clamp(p.protectBigOnceMem, 0.0, 30.0);
		int ffSel = -1;                                                   // s12 fix lane 2 (brain.fightersFirst): the no-idle pick taken over a far first strike
		for (int pass = 0; pass < 3; ++pass) {
			int i = ext ? (pass == 0 ? extIdx : -1) : Pick(f, p, cx, cur);
			// R6 step 8.1: the pick goes through the protect guard (keep a threat, no flip back, the hurter first); a kept
			// current target runs the loop below like any pick (its give-up clocks, sight, unreach rules all apply)
			if (!ext && _protCfg.on) i = ProtectFilter(f, p, cur, i);
			// s12 fix lane 2 (brain.fightersFirst; s12 gate dt 1080u 21:44:04.935: the soak raider FF00124F - combat target the
			// player, a thrown weapon, given up 'unreach' 7 s before and held by the no-idle pick - was left for the world raider
			// 00185233, a far first strike (FP: hostile, fighting nobody), then for the street raiders it woke; FF00124F was never
			// killed): a far first strike is never taken while a kept foe fighting the group is eligible for the no-idle pick -
			// that one is taken instead, by the no-idle fallback below exactly as when nothing is picked (FP ranks below every
			// fighting far foe, so such a foe is one Pick passed over: ignored / given up / hidden - this loop's give-up clocks
			// would give it up again every frame; the fallback keeps its own rotation, sight and reach restart)
			if (!ext && i >= 0 && p.fightersFirst >= 0.5 && p.noIdle >= 0.5 && _info[i].tier == Tier::kFarP && !cs[i].combatWithGroup && !cs[i].attackingPlayer) {
				const int j = NoIdlePick(f, p, cx);
				if (j >= 0 && j != i) {
					ffSel = j;
					++_st.fightersFirst;
					break;
				}
			}
			if (i < 0) break;
			++_st.picks;
			const Cand& c = cs[i];
			Info&       in = _info[i];
			Mem&        m = *in.m;
			const bool  isCur = c.id == _target;
			// leash: no ignore; picked again once he is back inside (pinners exempt)
			// ROUND5 B (user 21:15, ghoul swarm: 44 'give up ... leash' in 26 s; FF001F39 near the player dropped 5x while a far
			// one was re-picked every 0.1 s and its hunt ended at once for that threat): a foe around the player (within
			// guardStill) and clearly closer to the player than he is (by leashExtra) is on his way back, so the leash does not
			// drop it. Far foes keep the leash. brain.leashToCentre 0 = round 5 A.
			const bool towardCentre = (p.leashToCentre >= 0.5 && in.dCentre <= p.guardStill && in.dCentre + p.leashExtra < cx.dMeCentre) ||
			                          (NearPlayerFoe(p, c, in) && in.dCentre + p.leashExtra < cx.dMeCentre);  // s11 fix lane 2 (farHuntPlayerR)
			// (R6 step 4: the Warden's range rule is the leash)
			if (!ext && p.leashPick < 0.5 && !in.pin && !towardCentre && cx.dMeCentre > in.reach + p.leashExtra) {  // DPREP-T: leashPick 1 leashes before Pick
				in.excluded = true;
				in.leashed = true;
				++_st.leashDrops;
				out.giveUp = GiveUp::kLeash;
				out.giveUpId = c.id;
				continue;
			}
			// hunt caps: held this long without a hit
			if (isCur && !in.pin && !extShield) {
				const bool   isFarT = IsFar(in.tier);
				const double capT = isFarT ? p.huntTargetMax : p.huntMaxTime;
				if (_heldT >= capT - kTiny) {
					++_st.giveUpsHunt;
					if (ext) {  // R6 step 4
						ExtReachFail(c, GiveUp::kHunt, out);
						_heldT = 0;
					} else {
						GiveUpTarget(c.id, now, isFarT ? p.huntIgnore : p.huntIgnoreNear, GiveUp::kHunt, out);
						in.excluded = true;
						continue;
					}
				}
			}
			// sight: the current target was traced above; a new pick gets the multi-ray while the budget lasts
			const bool v = (i == cur && curTraced) ? curVis : TargetSight(c, m, f, p, w, 3);
			if (v) {
				m.blindT = -1;
			} else if (extShield) {
				// R6 step 4: the shield case (no reachable band-D foe): he hovers between it and the player; no give-up clocks
			} else {
				bool fpKeep = false;
				// (step-2 fixer, s2fBigFirst: a big hostile within d2DbBigR of the player is gone for while hidden too - the reach chain)
				const bool bigNP = p.s2fBigFirst >= 0.5 && c.cls == Cls::kBig && !c.essential && in.dPlayer <= p.d2DbBigR;
				// (R6 step 4: the Warden's first-strike sight rules decide; a hidden first strike it keeps is chased)
				if (!ext && (in.tier == Tier::kPreempt || in.tier == Tier::kFarP) && !in.committed && !bigNP && !InteriorHuntOk(c, cx)) {  // K3: a non-combat hostile is only attacked in sight (ROUND5: not in his own interior)
					// DPREP-T (live 21:38:24.2-26.9: 'HUNT|start 0011AE50 tier=FP ... end why=drop' 3x in 1.6 s at D 3961 /
					// 3056 / 1999, then 0011ADFB the same: a far first-strike target hidden for one trace was dropped, seen
					// again, re-picked): the CURRENT one is dropped only when not seen for fpHiddenDrop s (he keeps flying at
					// it, no swings: mayAttack needs sight); a dropped one waits for fpRepickSeen s of steady sight (Classify)
					if (p.fpHiddenDrop > 0 && isCur && now - m.visTrueAt < p.fpHiddenDrop - kTiny) {
						fpKeep = true;
						++_st.fpHiddenKeeps;  // frames
					} else {
						if (p.fpHiddenDrop > 0) {
							m.fpDropAt = now;
							m.why = "hidden";
							++_fpDropN;
						}
						in.excluded = true;
						continue;
					}
				}
				if ((in.tier == Tier::kPreempt || in.tier == Tier::kFarP) && !in.committed && !fpKeep) ++_st.interiorHunts;  // ROUND5 (frames)
				if (in.committed && (in.tier == Tier::kPreempt || in.tier == Tier::kFarP)) ++_st.commits;  // ROUND4: kept by the chain (frames)
				// K9 (ROUND3): without the navmesh, only a real interior cell (brain.k9Headroom 1: the old headroom rule), and only
				// beyond interiorChaseR; nearer hidden F2/F3 are AI-chased like local foes (the chase's own give-up applies).
				const bool k9In = p.k9Headroom >= 0.5 ? f.indoors : f.interiorCell;
				if (!navOn && IsFar(in.tier) && k9In && in.tier != Tier::kFar1 && Length(c.chest - f.meChest) > p.interiorChaseR) {
					++_st.giveUpsInterior;
					if (ext) {
						ExtReachFail(c, GiveUp::kInteriorBlind, out);  // R6 step 4: the AI chase goes on (NoNavHidden)
					} else {
						GiveUpTarget(c.id, now, p.interiorBlindIgnore, GiveUp::kInteriorBlind, out);
						in.excluded = true;
						continue;
					}
				}
				const double hitAnyC = now - std::max({ c.lastHitGroupAt, c.lastHitPlayerAt, c.lastHitHimAt });
				if (IsLocal(in.tier) && !in.pin) {
					// the blind clock waits while a viable sky path is flown (BRAIN §5.3); ROUND3: porches fly it too
					const bool planOk = isCur && (navOn || !f.interiorCell) && _planFor == c.id && !_planForReturn &&
					                    (_plan.kind == Plan::kOver || _plan.kind == Plan::kSide) && _exhaustedSince < 0;
					// ROUND3 targeting: ... and while the AI chase makes progress on a foe that fights / hit the group
					const bool chaseOk = p.chaseBlindPause >= 0.5 && isCur && _chaseSent && _aiChasing && in.tier != Tier::kPreempt &&
					                     (c.combatWithGroup || hitAnyC <= p.hitMemory) && now - _progAt < p.chaseBlindProg - kTiny;  // progress within chaseBlindProg s
					// ROUND3 REACH: ... and while the stage chain runs (it has its own clocks and the navHiddenMax cap)
					const bool navOk = isCur && navOn && _nav.forId == c.id && _nav.ok && !_nav.dead && now - _nav.progAt < p.navNoProg + kTiny;
					// R6 step 9: ... and while the Router drives it (Facts::route.owns: the route's stuck ladder reports failures)
					const bool stageOk = isCur && navOn && ((_stage >= 0 && _stage <= 3) || (f.route.on && f.route.owns != 0 && f.route.owns == c.id));
					if (m.blindT < 0) m.blindT = 0;
					else if (!planOk && !chaseOk && !navOk && !stageOk) m.blindT += dt;
					const double lim = (c.cls != Cls::kRegular || in.tier == Tier::kAttacker) ? p.blindRanged : p.blind;
					if (m.blindT >= lim - 1e-6) {
						++_st.giveUpsBlind;
						if (ext) {
							ExtReachFail(c, GiveUp::kBlind, out);  // R6 step 4 (the blind clock starts again)
						} else {
							GiveUpTarget(c.id, now, p.ignoreFor, GiveUp::kBlind, out);
							in.excluded = true;
							continue;
						}
					}
				}
				if (IsFar(in.tier) && isCur && !f.interiorCell && !_aiChasing && !navOn) {  // ROUND3: porches too; not during the AI chase
					const bool np = _noPathSince >= 0 && now - _noPathSince >= p.noPathGiveUp - kTiny;
					const bool ex = _exhaustedSince >= 0 && now - _exhaustedSince >= p.noPathGiveUp - kTiny;
					if (np || ex) {
						const bool canHB = p.huntBlink >= 0.5 && Length(c.chest - f.meChest) > p.huntBlinkMin &&
						                   now - m.huntBlinkAt[0] >= p.huntBlinkWindow && now - _lastBlinkAt >= p.blinkGap;
						if (!(canHB && TryBlink(c, f, p, w, out, 2))) {
							++_st.giveUpsUnreach;
							if (ext) {
								ExtReachFail(c, GiveUp::kNoPath, out);  // R6 step 4: a fresh plan next frame
							} else {
								GiveUpTarget(c.id, now, p.huntIgnore, GiveUp::kNoPath, out);
								in.excluded = true;
								continue;
							}
						}
					}
				}
				// explore m1 fix lane 3 (brain.noHitPin): a pinner has no give-up below, so a chain on it that ENDED (stage 4) would hold him
				// still (ReachHidden's kHold) beside the foe beating the player; it starts over instead (the straight approach and its no-hit
				// clock, then a fresh chain - the navmesh first). The Router's stage and the shield keep theirs
				if (navOn && isCur && in.pin && p.noHitPin >= 0.5 && _stage >= 4 && _stage != kRouteStage && !extShield && !TeleShielding(c.id)) {
					RestartReach(c);
					++_st.m1PinRestarts;
				}
				// ROUND3 REACH: the chain's end, and the hidden cap (hidden time only)
				if (navOn && isCur && !in.pin && !extShield && !TeleShielding(c.id)) {  // (R6 step 10: not while he holds the shield on it)
					// ROUND4 (in game R3: museum foes given up at ~10 s while the AI walk / nav leg still closed in): the cap counts
					// hidden time without progress (closer by progressStep, or nav progress, within hiddenProgGrace s); a hard
					// cap navHiddenHard still ends any hidden chase
					_hiddenTotal += dt;
					if (now - _progAt > p.hiddenProgGrace + kTiny) _hiddenT += dt;
					if (_stage >= 4 || _hiddenT >= p.navHiddenMax - kTiny || _hiddenTotal >= p.navHiddenHard - kTiny) {
						// ROUND5 A2 (gate A1: hidden-cap give-ups at the museum while the blink was never tried): a hidden cap that
						// ends the chain before its blink stage tries the blink first (once per target, within navBlinkSeenMax u);
						// a short window, then the give-up. brain.hiddenCapBlink 0 = round 5 A.
						const double dC = Length(c.chest - f.meChest);
						if (_stage < 3 && p.hiddenCapBlink >= 0.5 && p.navBlink >= 0.5 && !_capBlinkTried && _reachBlinks < static_cast<int>(p.navBlinksPer) &&
							dC <= p.navBlinkSeenMax && dC >= p.navBlinkNear && !(p.d2BlinkExterior < 0.5 && !f.interiorCell && !TeleOn(f)) &&
							(TeleOn(f) ? TeleCase(c, f) : ExtBlinkOk(c, f))) {  // D2: no cap blink outdoors (R6 step 4: nor without a blink permit; R6 step 10: the BLINK stage = the shield of an attacker of the player)
							_capBlinkTried = true;
							++_st.capBlinks;
							_triedNav = _triedRay = _aiTried = true;
							StageTo(3, "hiddenCap");
							_hiddenT = std::min(_hiddenT, std::max(0.0, p.navHiddenMax - 2.0));
							_hiddenTotal = std::min(_hiddenTotal, std::max(0.0, p.navHiddenHard - 2.0));
						} else {
							++_st.giveUpsUnreach;
							if (ext) {
								ExtReachFail(c, GiveUp::kUnreach, out);  // R6 step 4: the chain starts over from the navmesh
							} else if (DefendReReach(c, f, p, out)) {
								// s7.5 fix lane 3: an attacker of the player is kept; the chain starts over (blink in view indoors)
							} else {
								GiveUpTarget(c.id, now, (IsFar(in.tier) || (in.tier == Tier::kPreempt && !c.combatWithGroup)) ? p.huntIgnore : p.ignoreFor, GiveUp::kUnreach, out);  // ROUND5: an idle one he could not reach waits huntIgnore too
								in.excluded = true;
								continue;
							}
						}
					}
				}
			}
			// unreachable (L4D2 Unreachable; far legs: HUNT §4.6; AI chase: K9 6 s)
			// (R6 step 4: not in the shield case - he holds the shield spot on purpose; R6 step 10: nor while the Pilot's shield holds)
			if (isCur && !extShield && !TeleShielding(c.id)) {
				const double d = Length(c.chest - f.meChest);
				if (d < p.targetClose || d < _best - p.progressStep) {
					_best = std::min(_best, d);
					_progAt = now;
				}
				if (d < p.targetClose || d < _farBest - p.farProgress) {
					_farBest = std::min(_farBest, d);
					_farProgAt = now;
					_farFails = 0;
				}
				if (_stuck.Stuck(p)) {
					if (_stuckSince < 0) _stuckSince = now;
				} else {
					_stuckSince = -1;
				}
				// ROUND3 REACH: a nav / ray leg of the chain re-plans when stuck instead of giving up; nav progress keeps the
				// far-progress clock fresh (a path that first leads away round a building is progress)
				const bool navFlying = navOn && (_stage == 0 || _stage == 1) && _nav.forId == c.id;
				// (ROUND5 A2: not while he sits at the end of a used-up leg short of an off-navmesh foe)
				if (navOn && _nav.forId == c.id && _nav.ok && !_nav.dead && _nav.endSince < 0 && now - _nav.progAt < p.navNoProg) {
					_progAt = now;
					_farProgAt = now;
				}
				// step-2 fixer (s2fDefendBlink; soak15 downtown 14:06:46-14:07:35: 47 s at d 741-824 u from FF001816, 14 give-ups
				// 'unreach' each re-picked by the no-idle rule, the chain restarting at 'sky' every time, while it took the player
				// from 75 % to 3 %): this target hit the player within d2DbHitWin s, the player is below s2fDefendHp, and he got no
				// closer for s2fDefendNoProg s -> a defend blink beside it (in view and outdoors too: ARCH 7.5 allows defend jumps
				// below 50 %), at most one per blinkGap s
				if (p.s2fDefendBlink >= 0.5 && !in.pin && d >= p.navBlinkNear) {
					const double hpNow = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
					const bool   hurtBy = c.attackingPlayer && now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny && hpNow < p.s2fDefendHp;
					if (hurtBy && now - _progAt >= p.s2fDefendNoProg - kTiny && now - _lastBlinkAt >= p.blinkGap - kTiny && now - _lastBlinkTryAt >= 0.5 - kTiny &&
						TryBlink(c, f, p, w, out, 2))
						++_s2fDefBlinkN;
				}
				// ROUND4 fix 2: the AI / BLINK stages of the chain own their own clocks (navAiNoProg, the blink wait, the hidden
				// caps): a stuck verdict left over from the navmesh leg must not give the target up the moment the AI walk starts
				const bool chainLate = navOn && _stage >= 2 && _stage <= 3 && p.stuckProgGrace > 0;
				if (chainLate) _stuckSince = -1;
				bool unreach = !navFlying && !chainLate && _stuckSince >= 0 && now - _stuckSince >= p.stuckGiveUp - kTiny;
				// ROUND5 A2 (gate A1, museum upstairs at 60 fps: on a sight flicker the chain was reset, the approach pressed on
				// the balcony and the stuck law gave up "unreach" - 6 times, never killed): a near foe's stuck give-up hands it to
				// the reach chain first (visStuckChain s; the chain's own caps and its blink end it). brain.stuckToChain 0 = off.
				if (unreach && p.stuckToChain >= 0.5 && navOn && !in.pin && d <= p.handR && _stage < 3 && now >= _stuckChainUntil) {
					unreach = false;
					_stuckSince = -1;
					_stuckChainUntil = now + p.visStuckChain;
					++_st.stuckToChains;
				}
				if (navFlying && _stuckSince >= 0 && now - _stuckSince >= p.stuckGiveUp - kTiny) {
					_nav.replanNow = true;
					_stuckSince = now;
				}
				// ROUND3 SPEED (S5): the K9 6 s no-progress clock follows the real AI chase (porches fly); with the navmesh the
				// stage chain replaces it
				const bool chasing = !v && !navOn && _aiChasing;
				// ROUND4 fix 2 (in game R1, Concord deathclaw seen 230-330 u away behind ruin geometry: the chain ran its sky
				// stage, then the seen no-progress rule gave up and ignored it): while the reach chain runs on this seen target,
				// no progress for noProgress s moves the chain on (NAV, AI, BLINK) instead; past the BLINK stage it gives up.
				// brain.visNoProgChain 0 = the round-4 rule.
				const bool visChain = v && navOn && p.visNoProgChain >= 0.5 && _stage >= 0;
				if (!unreach && visChain && _stage >= 0 && _stage <= 3 && d <= p.handR) {
					if (now - _progAt >= p.noProgress - kTiny) {
						++_st.visNoProgChains;
						NextStage("visNoProg");
						_progAt = now;
					}
				} else if (!unreach && visChain && _stage >= 4 && d <= p.handR) {
					unreach = true;
				} else if (!unreach) {
					// ROUND3 fix: the open-sky no-path fallback chase keeps the round-2 no-path timing when it gets nowhere
					if (chasing) unreach = now - _progAt >= (_noPathChase ? p.noPathGiveUp : p.chaseNoProgress) - kTiny;
					else if (v && d > p.handR) {
						if (now - _farProgAt >= p.farNoProgress - kTiny) {
							++_farFails;
							_farProgAt = now;
							_planAt = -1e9;  // re-plan
							unreach = _farFails >= static_cast<int>(p.farReplans);
						}
					} else if (v) {
						unreach = now - _progAt >= p.noProgress - kTiny;
					}
				}
				if (unreach && !in.pin) {
					const double hpGu = f.playerHealthPct >= 0 ? f.playerHealthPct : _verdictHealth;
					const bool   defGu = p.s2fDefendBlink >= 0.5 && c.attackingPlayer && now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny && hpGu < p.s2fDefendHp &&
					                     d >= p.navBlinkNear && now - _lastBlinkAt >= p.blinkGap;  // step-2 fixer: a defend blink before the give-up
					const bool canHB = (p.huntBlink >= 0.5 && in.tier != Tier::kPreempt && d > p.huntBlinkMin &&
					                   now - m.huntBlinkAt[0] >= p.huntBlinkWindow && now - _lastBlinkAt >= p.blinkGap) || defGu;
					if (!(canHB && TryBlink(c, f, p, w, out, 2))) {
						++_st.giveUpsUnreach;
						if (ext) {
							ExtReachFail(c, GiveUp::kUnreach, out);  // R6 step 4: kept; progress / stuck clocks from now
						} else if (DefendReReach(c, f, p, out)) {
							// s7.5 fix lane 3: an attacker of the player is kept; the chain starts over (blink in view indoors)
						} else {
							GiveUpTarget(c.id, now, (IsFar(in.tier) || (in.tier == Tier::kPreempt && !c.combatWithGroup)) ? p.huntIgnore : p.ignoreFor, GiveUp::kUnreach, out);  // ROUND5: an idle one he could not reach waits huntIgnore too
							in.excluded = true;
							continue;
						}
					}
				}
			}
			sel = i;
			vis = v;
			break;
		}
		// HOTFIX round5C D (live round5C 03:39:31-33.5, 03:41:10-19, 03:41:58-03:42:12: 'tgt=0' with 9-11 kept foes while
		// they shot the player; the player died at 03:39:39): nothing was picked, yet a kept foe is attacking the player ->
		// the best of those foes is the target anyway (ignored / hidden / given up do not count; spent, kill-pending, resting
		// essentials and every hard veto do). brain.noIdle 0: round 5 C.
		_noIdleSel = false;  // FINISH B2 fix lane 4 (brain.noIdleNoResume): set below when the no-idle fallback gave this frame's target
		if (!ext && sel < 0 && p.noIdle >= 0.5) {  // (R6 step 4: the Warden's INV-2 is the no-idle rule)
			const int i = ffSel >= 0 ? ffSel : NoIdlePick(f, p, cx);  // (s12 fix lane 2: brain.fightersFirst already asked it this frame)
			if (i >= 0) {
				sel = i;
				_noIdleSel = true;
				vis = (i == cur && curTraced) ? curVis : TargetSight(cs[i], *_info[i].m, f, p, w, 3);
				if (vis) _info[i].m->blindT = -1;
				// s7.5 fix lane 3 (brain.defendReReach; run 123733: the fallback held the given-up turret at stage 4, move=X
				// vDes=0, 1.5 s): a reach chain that ended is started over instead of holding him still
				if (p.defendReReach >= 0.5 && i == cur && cs[i].id == _target && _stage >= 4) {
					RestartReach(cs[i]);
					++_st.noIdleRestarts;
				}
			}
		} else if (sel >= 0) {
			_fbId = 0;
		}
		if (sel >= 0 && cs[sel].attackingPlayer) ++_st.defendFirstPicks;

		// ---- target switch (instant re-target: only the aim changes)
		const std::uint32_t oldId = _target;
		const bool          oldFar = _tFar;
		const std::uint32_t newId = sel >= 0 ? cs[sel].id : 0;
		if (newId != _target) {
			if (_target && newId) ++_st.switches;
			if (newId) ++_st.targets;
			_prot.NoteSwitch(_target, newId, now);  // R6 step 8.1: the protect clocks (flip back / dwell)
			out.targetChanged = true;
			_target = newId;
			_hurtMemId = 0;  // s10 fix lane 2 (brain.hurtChainMem): once per pick
			ResetTargetState();
			_tTier = sel >= 0 ? _info[sel].tier : Tier::kNone;
			if (IsFar(_tTier)) ++_st.farHunts;
			if (sel >= 0) {
				_best = _farBest = Length(cs[sel].chest - f.meChest);
				_progAt = _farProgAt = now;
			}
		} else if (sel >= 0) {
			_tTier = _info[sel].tier;
		}
		_tFar = IsFar(_tTier);
		if (_target) _heldT += dt;

		if (sel >= 0) {
			_retEndOff = _retWait = false;
			_returning = _recallFly = _returnBlocked = _returnStuck = false;
			_returnSince = _returnNoPathSince = -1;
			_d2PbSince = -1e9;  // D2: a fight: the put-back episode is over
			_d2PbN = 0;
			const Cand& c = cs[sel];
			// pinner blink (L4D2 PIN_BLINK_AFTER / GAP): hidden or unreachable this long
			if (_info[sel].pin) {
				const bool blocked = !vis || now - _progAt >= p.noProgress || (_stuckSince >= 0 && now - _stuckSince >= p.stuckGiveUp);
				if (!blocked) {
					_pinBlockedSince = -1;
					_telePinId = 0;  // R6 step 10: a reachable pinner is fought, not shielded
				} else if (_pinBlockedSince < 0) _pinBlockedSince = now;
				else if (now - _pinBlockedSince >= p.blinkAfter - kTiny && now - _lastBlinkAt >= p.blinkGap - kTiny &&
				         now - _lastBlinkTryAt >= p.blinkGap - kTiny) {
					const bool blinked = TryBlink(c, f, p, w, out, 1);
					// R6 step 10 (Facts::tele, ARCH 4.11 / 3.9.5; desktop B-SOAK: a refused pin blink left him in a still AI chase 2.4 km
					// from a shooter at the player for 5 min): a pinner that stays blocked and attacks the player is SHIELDED (the
					// permitted defend jump follows from there) - a pinner has no give-up of its own
					if (!blinked && TeleOn(f) && TeleCase(c, f) && _telePinId != c.id) {
						_telePinId = c.id;
						++_st.telePinShields;
					}
					// step 4 integrator (brain.extPinFail; in game cz CZ.b 14:10:51-57: a pinner beat the player inside the closed
					// cage for 6 s while the refused pin blink left him in the AI-chase stage - a pinner has no give-up, so the
					// Warden never heard of it): while the Warden acts, a pin blink that cannot happen is a reach failure (its
					// failed-twice rule, then the permitted defend jump)
					if (!blinked && ext && p.extPinFail >= 0.5) {
						ExtReachFail(c, GiveUp::kUnreach, out);
						_pinBlockedSince = now;
						++_st.extPinFails;
					}
				}
			} else {
				_pinBlockedSince = -1;
				_telePinId = 0;  // R6 step 10
			}
			// R6 step 4: the shield case hovers between the shooter and the player (ARCH §3.9.5); the Warden's permitted defend
			// jump (§4.11) is made from here or from the old movement's way to it
			// R6 step 8.1 (brain.protectBlindR u, 0 = off; s7.7c soak death 04:32:59-33:01: 1.5 s hung 650-780 u from the shooter
			// that killed the player, 'move=6/3 vDes=25 noProg=1.49 may=false'; desktop PR-01B: 20+ s hovering 237 u above a
			// hurter his crowd-blind eyes called hidden, the sky plan used up, vDes 0): a foe HURTING the player within this
			// distance that his eyes call hidden while he got no closer for protectBlindStall s is flown at and attacked as
			// seen - in a crowd his eye rays run out of re-casts on the raiders' bodies, a wall would not let the hits through
			// anyway (the touch lines), and the stuck / no-hit rules hand a real wall to the reach chain within about a second
			if (!vis && !f.ext.on && p.protectBlindR > 0 && !c.killPending) {
				const double dB = Length(c.chest - f.meChest);
				const bool   hurt = c.lastHitPlayerAt > -1e8 && now >= c.lastHitPlayerAt && now - c.lastHitPlayerAt <= std::max(0.0, p.protectHurtWin) + kTiny;
				if (hurt && dB <= p.protectBlindR && now - _progAt >= std::max(0.0, p.protectBlindStall) - kTiny) {
					vis = true;
					++_st.protBlindForces;  // frames
				}
			}
			if (extShield && c.id == f.ext.id) ShieldMove(f, p, out, sel, vis);
			else if (NearStall(c, f, p) && NearStallLeg(c, f, p, w, out)) {
				_telePinId = 0;  // s13 fix round 3 lane 4 (brain.nearStallR): stalled on an attacker at the player - to the player's side
			} else if ((TeleOn(f) || p.nearPlayerLeg > 0) && (!vis || _stage >= 0) && TeleNearLeg(c, f, p, w, out)) {
				_telePinId = 0;  // R6 s10 fix lane 3 (6b): an attacker at the player - he flies to the player's side, never a shield from afar
			} else if (TeleOn(f) && _telePinId != 0 && _telePinId == c.id) {
				// R6 step 10: the blocked pinner's shield (the target fields as ChooseMove sets them, then the shield move); a
				// tele.shieldRetry or a lost case falls back to the old movement this frame
				ChooseMove(f, p, cx, w, out, sel, vis);
				if (!TeleShield(c, f, p, w, out)) _telePinId = 0;
			} else ChooseMove(f, p, cx, w, out, sel, vis);
			// s4f (brain.extStageFail; in game cz 003601 / 004806 at Warden 2: a melee raider beat the player behind the cage bars
			// for 1.3-3.6 s while the chain worked door / blinkWait / the stage-4 hold; only its give-up reached the Warden, so the
			// defend jump came at 3 % / 23 % health): while the Warden acts, the chain reaching its BLINK stage on a target that
			// hit the player within d2DbHitWin s is a reach failure at once - one note per chain (ResetTargetState clears it);
			// the chain goes on. ARCH 4.11's defend-jump rules are unchanged (warden.cnlOnceHitting: its next hit permits it).
			if (ext && p.extStageFail >= 0.5 && !_stageNoted && _stage >= 3 && c.id == _target && !out.reachFail && c.attackingPlayer &&
				c.lastHitPlayerAt > -1e8 && now - c.lastHitPlayerAt <= p.d2DbHitWin + kTiny && now >= c.lastHitPlayerAt) {
				_stageNoted = true;
				ExtReachNote(c, "stage", out);
			}
			if (ext && f.ext.mayDefendJump && c.id == f.ext.id && !out.blink && now - _lastBlinkAt >= p.blinkGap - kTiny && now - _lastBlinkTryAt >= 0.1 - kTiny &&
				now - _djTryAt >= p.blinkGap - kTiny)  // s4 fix lane 2: an asked jump (made or refused) is asked again after blinkGap s
				DefendJump(c, f, p, w, out);
		} else {
			ReturnMove(f, p, cx, w, out);
		}
		// DPREP-T (G2, predHiddenZero): his velocity is a path leg, not a dash at a target (next frame's ranking)
		_onLeg = out.move == Move::kNav || out.move == Move::kOverTop || out.move == Move::kReturn || out.move == Move::kAiChase ||
		         out.reachStage >= 0 || (out.move == Move::kHunt && out.planKind != Plan::kDirect);
		// FINISH A2 lane 3 round 2 (brain.windowTrapT): the target this frame flies STRAIGHT at (next frame's held-frame test)
		_wtStraight = (out.target != 0 && out.target == _target && out.planKind == Plan::kDirect && (out.move == Move::kApproach || out.move == Move::kHunt)) ? out.target : 0;

		// ---- hunt edges (OnSPNHunt; SPN|HUNT|start / end why=)
		if (oldFar && oldId != _target && !out.huntEnd) {
			const char* why = "drop";
			if (out.giveUpId == oldId && out.giveUp != GiveUp::kNone) why = GiveUpName(out.giveUp);
			else if (cur < 0) why = "gone";
			else if (cs[cur].killPending) why = "dead";
			else if (_chainLock) why = "chain";
			else if (anyLocal) why = "threat";
			else if (!cx.farOpen) why = cx.farWhy;
			else if (_target) why = "switch";
			else if (_info[cur].m && _info[cur].m->why[0]) why = _info[cur].m->why;
			out.huntEnd = true;
			out.huntEndId = oldId;
			out.huntWhy = why;
		}
		if (_tFar && _target != oldId) out.huntStart = true;
		if (_tFar && out.move == Move::kApproach && !_attackSent) {
			out.huntAttack = true;
			_attackSent = true;
		}
		{
			const bool ret = out.move == Move::kReturn;
			out.returnStart = ret && !_wasReturning;
			_wasReturning = ret;
		}

		// put-backs from the stuck law (L4D2 StuckCheck): follow / return, and the hard rule anywhere
		if (_stuck.Stuck(p) && (out.move == Move::kFollow || out.move == Move::kReturn)) _stuckFollowFor += dt;
		else _stuckFollowFor = 0.0;
		// ROUND5 A2 (gate A1, Concord after the quest kill, c60-r5 17:09:23-26 and c120-r5-b 17:41:24-25: back in the native
		// follow 250-420 u from the player he pressed on something low for 1.5 s, stuckT 0.75 -> 1.76, then "putback why=2"):
		// a stuck native follow first flies back itself - the navmesh leg to the follow spot, then the flown push - and the
		// put-back comes only if that return gets nowhere (its returnPushFor) or after stuckReturnGap s it is stuck again.
		// brain.stuckReturn 0 = round 5 A.
		const Vec3   folSpot = f.haveFollowSpot ? f.followSpot : f.playerFeet;
		const double dFolSpot = Length(folSpot - f.meFeet);
		const bool   stuckFollowNow = _stuckFollowFor >= p.stuckNeed - kTiny && dFolSpot > p.returnDoneNear && cx.dMePlayer > p.stuckPutBackNear;
		const bool   canStuckReturn = p.stuckReturn >= 0.5 && p.navFlyPush >= 0.5 && out.move == Move::kFollow && !_returning && !f.waiting &&
		                            now >= _noReturnUntil && now - _stuckReturnAt >= p.stuckReturnGap - kTiny;
		if (canStuckReturn && (stuckFollowNow || _stuckFor >= p.stuckHard - kTiny)) {
			const bool navOkR = p.nav >= 0.5 && p.returnNav >= 0.5 && w.NavAvailable();
			_returning = true;
			_returnBlocked = true;
			_returnStuck = true;
			_retEndOff = _retWait = false;
			_returnClear = false;
			_returnSince = now;
			_returnNoPathSince = -1;
			_returnCheckAt = -1e9;
			if (_nav.forId == kReturnNavId) _nav = NavState{};
			_retBest = _retStart = dFolSpot;
			_retProgAt = now;
			_retEsc = navOkR ? 1 : 2;
			_pushFor = 0;
			_stuckReturnAt = now;
			_stuck.Reset();
			_stuckFor = _stuckFollowFor = 0;
			_stuckSince = -1;
			++_st.stuckReturns;
		} else if (_stuckFor >= p.stuckHard - kTiny) {
			RequestPutBack(PutBackWhy::kStuck, out);
		} else if (_stuckFollowFor >= p.stuckPutBackAfter - kTiny && cx.dMePlayer > p.stuckPutBackNear) {
			RequestPutBack(PutBackWhy::kStuck, out);
		}

		// the others' sight, round robin, inside the base budget only
		RoundRobin(f, p, w);

		// vetting queue + request edge
		BuildVet(f, p, cx);
		bool wantVet = false;
		for (int i = 0; i < n && !wantVet; ++i) {
			const Info& in = _info[i];
			if (!in.pending) continue;
			if (in.pendTier == Tier::kPreempt) wantVet = sel < 0 || !IsLocal(_tTier);
			else wantVet = sel < 0 || (IsFar(_tTier) && FarRank(in.pendTier) > FarRank(_tTier));
		}
		if (wantVet && now - _lastVetReqAt >= p.vetRequestGap - kTiny) {
			_lastVetReqAt = now;
			out.vetRequest = true;
			++_st.vetRequests;
		}

		_st.blindFor = (sel >= 0 && _info[sel].m && _info[sel].m->blindT > 0) ? _info[sel].m->blindT : 0.0;
		_st.noProgFor = _target ? now - _progAt : 0.0;
		// s13 fix r1 lane 2 (brain.essHoldStay): he hovers by the held essential through its stay window - no swing at it
		if (_target && out.mayAttack && EssStaying(_target, now)) out.mayAttack = false;
		finish();
	}
}
