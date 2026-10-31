// OWNER: R6 (round 6 step 10, CORE lane). See pilot.h.
#include "core/pilot.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sp::pilot
{
	namespace
	{
		constexpr double kNever = -1e9;
		std::size_t      I(Kind k) { return static_cast<std::size_t>(k); }
	}

	const char* KindName(Kind k)
	{
		switch (k) {
		case Kind::kPin: return "pin";
		case Kind::kHunt: return "hunt";
		case Kind::kReach: return "reach";
		case Kind::kValve: return "valve";
		case Kind::kReturn: return "return";
		case Kind::kStuck: return "stuck";
		case Kind::kInsideSolid: return "insideSolid";
		case Kind::kDefendJump: return "defend";
		case Kind::kLost: return "lost";
		case Kind::kWedged: return "wedged";
		default: return "none";
		}
	}

	Kind KindOfBlink(int a_blinkWhy, bool a_lost)
	{
		switch (a_blinkWhy) {
		case 1: return Kind::kPin;
		case 2: return Kind::kHunt;
		case 3: return Kind::kReach;
		case 4: return a_lost ? Kind::kLost : Kind::kReturn;
		case 5: return Kind::kDefendJump;
		case 6: return Kind::kWedged;
		default: return Kind::kNone;
		}
	}

	Kind KindOfPutBack(int a_putBackWhy)
	{
		switch (a_putBackWhy) {
		case 1: return Kind::kReturn;
		case 2: return Kind::kStuck;
		case 3: return Kind::kLost;
		default: return Kind::kNone;
		}
	}

	void Policy::Reset()
	{
		_p = Permit{};
		_shieldId = 0;
		_shieldSince = _shieldLastAt = kNever;
		_hitDuring = false;
		_onLineFor = 0;  // s10 fix lane 3
		_onLine = false;
		_otherFor = 0;
		_otherSeen = false;
		_lostBest = -1;
		_lostProgAt = kNever;
		_lastNow = kNever;
		_jumps.fill(kNever);
		_jumpN = 0;
		_wedgedAt = kNever;
		// (the counters stay: they are the session's)
	}

	void Policy::OnJump(double a_now)
	{
		// a continuity event (a load door, fast travel, a script MoveTo; never our own jump): the separation clocks and the
		// shield start over from the new place (ARCH §4.13). The budget and the wedged clock are wall clock and stay.
		_shieldId = 0;
		_shieldSince = _shieldLastAt = kNever;
		_hitDuring = false;
		_onLineFor = 0;  // s10 fix lane 3
		_onLine = false;
		_lostBest = -1;
		_lostProgAt = a_now;
		_p.shield = _p.mayDefendJump = false;
		_p.shieldId = _p.jumpId = 0;
		// (_otherFor is kept: "the player in another space for > 3 s" is measured across the player's own load screen)
	}

	int Policy::JumpsInWindow(double a_wall, const Cfg& c) const
	{
		int n = 0;
		for (double t : _jumps)
			if (t > -1e8 && a_wall >= t && a_wall - t <= c.budgetWindow) ++n;
		return n;
	}

	bool Policy::WedgedDue(double a_wall, const Cfg& c) const { return _wedgedAt < -1e8 || a_wall < _wedgedAt || a_wall - _wedgedAt >= c.wedgedEvery; }

	const Permit& Policy::Update(const Frame& f, const Cfg& c)
	{
		Permit       p{};
		const double hp = (std::isfinite(f.hp) && f.hp >= 0) ? std::clamp(f.hp, 0.0, 1.0) : 1.0;  // unknown = 1.0 (HOOKS s10 C8)
		const double dt = std::clamp(f.dt, 0.0, 0.25);
		p.hp = hp;
		p.free = hp < c.freeHp;

		// ---- the shield clock (ARCH §3.9.5 / §4.11 "the shield was held 1.0 s and the player was hit again during it")
		if (f.shieldId && !f.carrying) {
			const bool fresh = f.shieldId != _shieldId || _shieldLastAt < -1e8 || f.now < _shieldLastAt || f.now - _shieldLastAt > c.shieldLinger;
			const double step = fresh ? 0.0 : std::max(0.0, f.now - _shieldLastAt);
			if (fresh) {
				_shieldId = f.shieldId;
				_shieldSince = kNever;
				_hitDuring = false;
				_onLineFor = 0;
				_onLine = false;
				++_st.shields;
			}
			_shieldLastAt = f.now;
			// s10 fix lane 3 (6a): the clock runs only while he blocks the line (his chest within shieldOnLine u of it; unknown =
			// on it); "hit again during it" counts from the moment he first got on it. Always on the line = step 10 exactly.
			const bool onLine = !(c.shieldOnLine > 0) || !(f.dLine >= 0) || f.dLine <= c.shieldOnLine;
			if (onLine) {
				if (!_onLine) {
					_onLine = true;
					_shieldSince = f.now;
					_hitDuring = false;
				} else {
					_onLineFor += step;
				}
			}
			// hit again during it: any hit on the player after it started; or the target's hit on HIM (his body took the shot)
			if (_onLine && (f.anyHitPlayerAt > _shieldSince || f.tHitPlayerAt > _shieldSince || (c.shieldHitHim && f.tHitHimAt > _shieldSince))) _hitDuring = true;
			++_st.shieldFrames;
		} else if (_shieldId && (f.carrying || f.now < _shieldLastAt || f.now - _shieldLastAt > c.shieldLinger)) {
			_shieldId = 0;
			_shieldSince = _shieldLastAt = kNever;
			_hitDuring = false;
			_onLineFor = 0;
			_onLine = false;
		}
		p.shield = _shieldId != 0 && f.shieldId == _shieldId;
		p.shieldId = p.shield ? _shieldId : 0;
		p.shieldFor = p.shield ? _onLineFor : 0.0;
		p.hitDuring = p.shield && _hitDuring;

		// ---- the defend jump permission (ARCH §4.11)
		p.budgetUsed = JumpsInWindow(f.wall, c);
		const bool hitRecently = (f.tHitPlayerAt > -1e8 && f.now >= f.tHitPlayerAt && f.now - f.tHitPlayerAt <= c.hitWindow) ||
		                         (p.shield && c.shieldHitHim && f.tHitHimAt > -1e8 && f.now >= f.tHitHimAt && f.now - f.tHitHimAt <= c.hitWindow);
		const char* why = "ok";
		if (!c.defend) why = "off";
		else if (f.carrying) why = "carry";
		else if (!p.shield) why = "noShield";
		else if (!f.jumpOnly) why = "reach";
		else if (f.otherDReachable) why = "otherD";
		else if (!hitRecently) why = "notHit";
		else if (!p.free && !(p.shieldFor >= c.shieldFirst - 1e-9 && p.hitDuring)) why = "shieldFirst";
		else if (!p.free && p.budgetUsed >= c.budget) why = "budget";
		p.jumpWhy = why;
		p.mayDefendJump = why[0] == 'o' && why[1] == 'k';
		p.jumpId = p.mayDefendJump ? p.shieldId : 0;
		p.jumpInViewOk = p.mayDefendJump && hp < c.inViewHp && hitRecently;
		if (p.mayDefendJump) ++_st.permitFrames;

		// ---- the lost transfer (ARCH §4.11): another space for > lostSeconds, or > lostDist without progress for lostNoProg s
		if (f.spaceKnown) {
			// (from the first frame the separation is SEEN: that frame's dt lies before it - "> 3 s" is never early)
			_otherFor = f.otherSpace ? (_otherSeen ? _otherFor + dt : 0.0) : 0.0;
			_otherSeen = f.otherSpace;
			if (!f.otherSpace && f.guardDist > c.lostDist) {
				if (_lostBest < 0 || f.guardDist < _lostBest - c.lostProgStep) {
					_lostBest = f.guardDist;
					_lostProgAt = f.now;
				}
			} else {
				_lostBest = -1;
				_lostProgAt = f.now;
			}
		}  // (unknown space: every clock holds - HOOKS s10 C7)
		const double noProg = (_lostBest >= 0 && f.now >= _lostProgAt) ? f.now - _lostProgAt : 0.0;
		p.otherSpace = f.spaceKnown && f.otherSpace;
		p.otherFor = _otherFor;
		p.noProgFor = noProg;
		const bool bySpace = _otherFor > c.lostSeconds;
		const bool byFar = noProg >= c.lostNoProg;
		p.mayLost = !f.carrying && (bySpace || byFar);
		p.lostWhy = bySpace ? "space" : byFar ? "far" : "-";
		p.lostFor = std::max(bySpace ? _otherFor - c.lostSeconds : 0.0, byFar ? noProg - c.lostNoProg : 0.0);
		_lastNow = f.now;
		_p = p;
		return _p;
	}

	Verdict Policy::Ask(Kind k, std::uint32_t a_id, bool a_seenFrom, bool a_seenTo, double a_wall, const Cfg& c)
	{
		Verdict v;
		const auto ki = I(k) < _st.asks.size() ? I(k) : 0;
		++_st.asks[ki];
		v.seen = a_seenFrom || a_seenTo;
		auto refuse = [&](const char* a_why) {
			v.ok = false;
			v.why = a_why;
			++_st.refused[ki];
			return v;
		};
		if (!KindAllowed(k)) return refuse("never");
		switch (k) {
		case Kind::kDefendJump:
			if (!c.defend) return refuse("off");
			if (!_p.mayDefendJump || a_id == 0 || a_id != _p.jumpId) {
				++_st.permitRefused;
				return refuse("noPermit");
			}
			if (v.seen && !_p.jumpInViewOk) {
				++_st.seenRefused;
				return refuse("seen");
			}
			if (!_p.free && JumpsInWindow(a_wall, c) >= c.budget) {
				++_st.budgetRefused;
				return refuse("budget");
			}
			break;
		case Kind::kLost:
			if (!_p.mayLost) {
				++_st.permitRefused;
				return refuse("notLost");
			}
			if (v.seen) {
				++_st.seenRefused;
				return refuse("seen");
			}
			break;
		case Kind::kWedged:
			if (v.seen) {
				++_st.seenRefused;
				return refuse("seen");
			}
			if (!WedgedDue(a_wall, c)) {
				++_st.everyRefused;
				return refuse("every");
			}
			break;
		default: return refuse("never");
		}
		v.ok = true;
		v.why = "ok";
		++_st.granted[ki];
		return v;
	}

	void Policy::Done(Kind k, std::uint32_t /*a_id*/, double a_wall, bool a_seen)
	{
		const auto ki = I(k) < _st.done.size() ? I(k) : 0;
		++_st.done[ki];
		if (k == Kind::kDefendJump) {
			// step 10 fix lane 1 (REVIEW W2, ARCH §4.11 "<= 2 per 600 s while the player is at or above 35 %; free below 35 %"):
			// only a jump made at or above freeHp uses the budget. Before, the free emergency jumps below 35 % filled the ring
			// too, and after a stimpak the next caged shooter's jump was refused "budget" for up to 10 min.
			if (!_p.free) {
				_jumps[static_cast<std::size_t>(_jumpN % static_cast<int>(_jumps.size()))] = a_wall;
				++_jumpN;
			}
			if (a_seen) ++_st.jumpsSeen;
			if (_p.free) ++_st.jumpsFree;
			// a fresh shield episode after the jump (the next jump needs its own shield second unless the player is low)
			_shieldSince = _shieldLastAt;
			_hitDuring = false;
			_onLineFor = 0;  // s10 fix lane 3 (6a): the next jump needs its own time on the line
		} else if (k == Kind::kWedged) {
			_wedgedAt = a_wall;
		} else if (k == Kind::kLost) {
			if (_p.otherSpace) ++_st.lostSpace;
			else ++_st.lostFar;
			_otherFor = 0;
			_otherSeen = false;
			_lostBest = -1;
			_lostProgAt = _lastNow;
		}
	}

	std::string Policy::Describe() const
	{
		std::uint64_t asks = 0, granted = 0, refused = 0;
		for (std::size_t i = 0; i < _st.asks.size(); ++i) {
			asks += _st.asks[i];
			granted += _st.granted[i];
			refused += _st.refused[i];
		}
		return std::format(" | PILOT tele asks={} granted={} refused={} (never={} seen={} budget={} permit={} every={}) done defend={} lost={} wedged={} jumpsSeen={} jumpsFree={} "
		                   "shields={} shieldFrames={} permitFrames={} lostSpace={} lostFar={} hp={:.2f} jumpWhy={} lostWhy={}",
			asks, granted, refused,
			_st.refused[I(Kind::kPin)] + _st.refused[I(Kind::kHunt)] + _st.refused[I(Kind::kReach)] + _st.refused[I(Kind::kValve)] + _st.refused[I(Kind::kReturn)] +
				_st.refused[I(Kind::kStuck)] + _st.refused[I(Kind::kInsideSolid)],
			_st.seenRefused, _st.budgetRefused, _st.permitRefused, _st.everyRefused, _st.done[I(Kind::kDefendJump)], _st.done[I(Kind::kLost)], _st.done[I(Kind::kWedged)],
			_st.jumpsSeen, _st.jumpsFree, _st.shields, _st.shieldFrames, _st.permitFrames, _st.lostSpace, _st.lostFar, _p.hp, _p.jumpWhy, _p.lostWhy);
	}

	bool Inv11(Kind k, bool a_permitted, bool a_seen, double a_hp, const Cfg& c)
	{
		if (!KindAllowed(k)) return false;
		if (!a_permitted) return false;
		if (k == Kind::kLost || k == Kind::kWedged) return !a_seen;
		if (k == Kind::kDefendJump) return !a_seen || (std::isfinite(a_hp) && a_hp >= 0 && a_hp < c.inViewHp);
		return false;
	}

	Vec3 PapyrusLanding(const Vec3& a_playerFeet, double a_playerHeading)
	{
		return { a_playerFeet.x + std::cos(a_playerHeading) * 100.0, a_playerFeet.y - std::sin(a_playerHeading) * 100.0, a_playerFeet.z };
	}

	Vec3 HeadingFwd(double a_heading) { return { std::sin(a_heading), std::cos(a_heading), 0.0 }; }

	int LostFirstSide(unsigned a_mask)
	{
		for (int side : kLostSides)
			if (a_mask & (1u << side)) return side;
		return -1;
	}

	Vec3 PapyrusSideSpot(const Vec3& a_playerFeet, const Vec3& a_fwd, int a_side, double a_dist)
	{
		double fx = a_fwd.x, fy = a_fwd.y;
		const double L = std::sqrt(fx * fx + fy * fy);
		if (!(L > 1e-9) || !std::isfinite(L)) {
			fx = 0;
			fy = 1;
		} else {
			fx /= L;
			fy /= L;
		}
		double dx = fy, dy = -fx;  // 0 right (Lib.SideSpot: heading + 90)
		if (a_side == 1) {
			dx = -fy;
			dy = fx;
		} else if (a_side == 2) {
			dx = -fx;
			dy = -fy;
		} else if (a_side == 3) {
			dx = fx;
			dy = fy;
		}
		return { a_playerFeet.x + dx * a_dist, a_playerFeet.y + dy * a_dist, a_playerFeet.z };
	}

	Vec3 ShieldSpot(const Vec3& a_playerChest, const Vec3& a_shooterChest, double a_dist)
	{
		// HOOKS s10 C15 / D: his torso ON the shooter -> player-chest line (the shooter's line-of-fire test meets his capsule,
		// its bullets his drawn body), a_dist from the player's chest along it, never past the shooter (40 u short of it)
		const Vec3   d{ a_shooterChest.x - a_playerChest.x, a_shooterChest.y - a_playerChest.y, a_shooterChest.z - a_playerChest.z };
		const double L = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (!(L > 1e-6) || !std::isfinite(L)) return a_playerChest;
		const double k = std::min(a_dist, std::max(0.0, L - 40.0)) / L;
		return { a_playerChest.x + d.x * k, a_playerChest.y + d.y * k, a_playerChest.z + d.z * k };
	}
}
