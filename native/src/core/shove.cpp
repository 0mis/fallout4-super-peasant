// OWNER: R6 STEP 15 (CORE lane). See core/shove.h.
#include "core/shove.h"

#include "config.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace sp::shove
{
	using namespace sp::math;
	namespace B = sp::body;

	namespace
	{
		double T(std::string_view a_k, double a_d) { return sp::cfg::Tunable(a_k, a_d); }
		Vec3   Mid(const Vec3& a, const Vec3& b) { return (a + b) * 0.5; }
		Vec3   Flat(const Vec3& v) { return { v.x, v.y, 0.0 }; }
		// the TABS order of his parts for the sweeps (the core he flies with first, then what hits)
		constexpr int kSweepOrder[B::kPartCount] = { B::kHip, B::kTorso, B::kFistR, B::kFistL, B::kHead, B::kKneeL, B::kKneeR, B::kLegL, B::kLegR, B::kElbowR, B::kElbowL,
			B::kArmR, B::kArmL };
		// ... and for the pushers (the legs kick the junk first; the fists only after the body)
		constexpr int kPusherOrder[B::kPartCount] = { B::kHip, B::kTorso, B::kHead, B::kKneeL, B::kKneeR, B::kLegL, B::kLegR, B::kFistR, B::kFistL, B::kElbowR, B::kElbowL,
			B::kArmR, B::kArmL };
	}

	Cfg Cfg::FromTunables()
	{
		Cfg c;
		c.on = T("look.bodyContacts", 0.0) != 0.0;
		c.foes = T("look.shoveFoes", 0.0) != 0.0;
		c.corpses = T("shove.corpses", 1.0) != 0.0;
		c.props = T("shove.props", 1.0) != 0.0;
		c.massGain = std::clamp(T("shove.massGain", 1.0), 0.05, 20.0);
		c.maxDvCorpse = std::clamp(T("shove.maxDvCorpse", 7.0), 0.0, 60.0);
		c.maxDvProp = std::clamp(T("shove.maxDvProp", 14.0), 0.0, 60.0);
		c.maxDvFoe = std::clamp(T("shove.maxDvFoe", 2.5), 0.0, 20.0);
		c.minVn = std::clamp(T("shove.minVn", 0.4), 0.0, 20.0);
		c.cooldown = std::clamp(T("shove.cooldown", 0.12), 0.0, 2.0);
		c.idleEvery = static_cast<int>(std::clamp(T("shove.idleEvery", 1.0), 1.0, 4.0));  // finB
		c.react = std::clamp(T("shove.react", 1.0), 0.0, 1.0);
		c.reactMaxDv = std::clamp(T("shove.reactMaxDv", 120.0), 0.0, 5000.0);
		c.budgetUs = std::clamp(T("shove.budgetUs", 200.0), 20.0, 2000.0);
		c.maxQueries = static_cast<int>(std::clamp(T("shove.maxQueries", 10.0), 0.0, 13.0));
		c.maxPushes = static_cast<int>(std::clamp(T("shove.maxPushes", 12.0), 0.0, 32.0));
		c.reach = std::clamp(T("shove.reach", 2.0), 0.0, 32.0);
		c.friendClearR = std::clamp(T("shove.friendClearR", 120.0), 0.0, 1000.0);
		c.awayDeg = std::clamp(T("shove.awayDeg", 60.0), 0.0, 180.0);
		c.awayR = std::clamp(T("shove.awayR", 1500.0), 0.0, 10000.0);
		c.nearPad = std::clamp(T("shove.nearPad", 64.0), 0.0, 1000.0);
		c.friendRecheck = T("shove.friendRecheck", 0.0) != 0.0;
		c.capFinal = T("shove.capFinal", 0.0) != 0.0;
		c.nearEvery = std::clamp(T("shove.nearEvery", 0.0), 0.0, 0.5);
		c.slowSkip = T("shove.slowSkip", 0.0) != 0.0;
		// B2 lane 3 (code defaults = finB)
		c.minPartV = std::clamp(T("shove.minPartV", 0.0), 0.0, 20000.0);
		c.victimR = std::clamp(T("shove.victimR", 0.0), 0.0, 1000.0);
		c.pass = T("shove.capsulePass", 0.0) != 0.0;
		c.passV = std::clamp(T("shove.passV", 900.0), 100.0, 20000.0);
		c.passLook = std::clamp(T("shove.passLook", 0.05), 0.01, 0.2);
		c.passPad = std::clamp(T("shove.passPad", 10.0), 0.0, 60.0);
		c.passClear = std::clamp(T("shove.passClear", 60.0), 0.0, 1000.0);
		c.passHold = std::clamp(T("shove.passHold", 0.25), 0.0, 5.0);
		c.passHoldMax = std::clamp(T("shove.passHoldMax", 3.0), 0.25, 10.0);
		c.passMax = static_cast<int>(std::clamp(T("shove.passMax", 48.0), 0.0, 64.0));
		c.govV = std::clamp(T("shove.govV", 0.0), 0.0, 20000.0);
		c.pushers = T("look.pushers57", 0.0) != 0.0;
		c.pCorpses = T("pushers.corpses", 0.0) != 0.0;
		c.pMax = static_cast<int>(std::clamp(T("pushers.max", 9.0), 0.0, 13.0));
		c.pMaxV = std::clamp(T("pushers.maxV", 1400.0), 0.0, 20000.0);
		c.pSolidHold = std::clamp(T("pushers.solidHold", 0.25), 0.0, 5.0);
		c.pNearR = std::clamp(T("pushers.nearR", 250.0), 0.0, 2000.0);
		c.pIdleKill = std::clamp(T("pushers.idleKill", 4.0), 0.5, 600.0);
		c.pQuietR = std::clamp(T("pushers.quietR", 300.0), 0.0, 3000.0);
		c.pLead = std::clamp(T("pushers.lead", 1.0), 0.0, 4.0);
		c.pMaxStepU = std::clamp(T("pushers.maxStep", 400.0), 10.0, 10000.0);
		c.pDrive = static_cast<int>(std::clamp(T("pushers.drive", 0.0), 0.0, 1.0));  // B2_lane2
		c.pSnapMaxU = std::clamp(T("pushers.snapMax", 64.0), 4.0, 1000.0);
		c.pFollowTolU = std::clamp(T("pushers.followTol", 120.0), 10.0, 5000.0);
		c.pFollowSec = std::clamp(T("pushers.followSec", 1.0), 0.1, 30.0);
		return c;
	}

	// ------------------------------------------------------------------ shapes
	PartShape FromContactShape(const B::ContactShape& a_s, double a_upm)
	{
		PartShape p;
		p.part = a_s.part;
		p.kind = a_s.kind;
		p.a0 = p.a1 = UnityToFO4Pos(a_s.a, a_upm);
		p.b0 = p.b1 = UnityToFO4Pos(a_s.b, a_upm);
		p.r = a_s.r * a_upm;
		p.vel = UnityToFO4Dir(a_s.vel) * a_upm;
		return p;
	}

	int MergeParts(std::span<const PartShape> a_shapes, const double* a_partMass, double a_massGain, double a_totalMass, PartSweep* a_out, int a_cap)
	{
		int n = 0;
		for (const int part : kSweepOrder) {
			if (n >= a_cap) break;
			Vec3   c0{}, c1{}, v{};
			int    k = 0;
			for (const auto& s : a_shapes) {
				if (s.part != part) continue;
				c0 += Mid(s.a0, s.b0);
				c1 += Mid(s.a1, s.b1);
				v += s.vel;
				++k;
			}
			if (k == 0) continue;
			c0 = c0 / static_cast<double>(k);
			c1 = c1 / static_cast<double>(k);
			v = v / static_cast<double>(k);
			double R = 0;
			for (const auto& s : a_shapes) {
				if (s.part != part) continue;
				const double hh = 0.5 * Length(s.b1 - s.a1);
				R = std::max(R, Length(Mid(s.a1, s.b1) - c1) + hh + s.r);
				const double hh0 = 0.5 * Length(s.b0 - s.a0);
				R = std::max(R, Length(Mid(s.a0, s.b0) - c0) + hh0 + s.r);
			}
			if (!IsFinite(c0) || !IsFinite(c1) || !IsFinite(v) || !(R > 0) || !std::isfinite(R)) continue;
			PartSweep& o = a_out[n++];
			o.part = part;
			o.c0 = c0;
			o.c1 = c1;
			o.R = R;
			o.vel = v;
			const double m = a_partMass ? a_partMass[part] : 0.0;
			o.mass = std::max(0.0, m * a_massGain);
			if (a_totalMass > 0) o.mass = std::min(o.mass, a_totalMass);
		}
		return n;
	}

	// ------------------------------------------------------------------ who is who
	const char* WhoName(Who a_w)
	{
		switch (a_w) {
		case Who::kProp: return "prop";
		case Who::kCorpse: return "corpse";
		case Who::kFoe: return "foe";
		case Who::kFriend: return "friend";
		case Who::kSelf: return "self";
		case Who::kStatic: return "static";
		case Who::kLiving: return "living";
		case Who::kSkip: return "skip";
		case Who::kPusher: return "pusher";
		case Who::kNearFriend: return "nearFriend";
		default: return "none";
		}
	}
	bool Pushable(Who a_w) { return a_w == Who::kProp || a_w == Who::kCorpse || a_w == Who::kFoe; }

	Who Classify(const BodyHit& a_h, const BodyInfo& a_i, const FrameIn& a_in, const Cfg& a_c, const Foe** a_foe)
	{
		if (a_foe) *a_foe = nullptr;
		if (a_h.group != 0 && a_h.group == a_in.selfGroup) return Who::kSelf;
		for (const auto& f : a_in.friends)
			if (f.group != 0 && f.group == a_h.group) return Who::kFriend;
		for (const auto g : a_in.skipGroups)
			if (g != 0 && g == a_h.group) return Who::kSkip;
		const std::uint32_t L = a_h.layer;
		if (L >= 64) return Who::kNone;
		const bool living = L == 30 || L == 8 || L == 33;
		if (living) {
			if (L != 30 || !a_c.foes || a_h.group == 0) return Who::kLiving;
			for (const auto& f : a_in.foes) {
				if (f.group == 0 || f.group != a_h.group) continue;
				if (!f.allowed) return Who::kLiving;
				if (!a_i.ok || !a_i.dynamic || a_i.keyframed || !(a_i.invMass > 0)) return Who::kStatic;
				if (a_foe) *a_foe = &f;
				return Who::kFoe;
			}
			return Who::kLiving;  // a living body nobody vouched for is never pushed
		}
		const bool corpse = L == 32;
		const bool prop = ((kLayProps >> L) & 1ull) != 0;
		if (!corpse && !prop) return Who::kNone;
		if (corpse && !a_c.corpses) return Who::kNone;
		if (prop && !a_c.props) return Who::kNone;
		// B2 lane 3 (gate finB hf 1080u: two bones of a corpse pushed 0.08 s after his punch launched it, then 2466 / 2551 u/s = step 13's
		// own launch): a corpse body right at a skipped actor (his target, a punch victim, a flung / height-watched body) is that actor's,
		// whatever group the engine reports for it now (a dead actor's group read can come back 0 / stale) - step 13 owns it
		if (corpse && a_c.victimR > 0)
			for (const auto& q : a_in.skipPos)
				if (IsFinite(q) && Length(a_h.p - q) < a_c.victimR) return Who::kSkip;
		if (!a_i.ok || !a_i.dynamic || a_i.keyframed || !(a_i.invMass > 0) || !std::isfinite(a_i.invMass)) return Who::kStatic;
		if (a_in.pushersSolid && ((a_in.pusherLayers >> L) & 1ull)) return Who::kPusher;
		for (const auto& f : a_in.friends) {
			const Vec3 d = a_h.p - f.pos;
			if (Length(Flat(d)) < a_c.friendClearR && d.z > -220.0 && d.z < 150.0) return Who::kNearFriend;
		}
		return corpse ? Who::kCorpse : Who::kProp;
	}

	// ------------------------------------------------------------------ the contact
	Vec3 PushDir(const Vec3& a_c0, const Vec3& a_c1, double a_R, const Vec3& a_p, const Vec3& a_nHit)
	{
		const Vec3   fb = Normalize(-a_nHit, Normalize(a_c1 - a_c0, { 1, 0, 0 }));
		const Vec3   D = a_c1 - a_c0;
		const Vec3   f = a_c0 - a_p;
		const double a = Dot(D, D);
		const double c = Dot(f, f) - a_R * a_R;
		Vec3         q = a_c0;
		if (c > 0 && a > 1e-9) {
			const double b = 2.0 * Dot(f, D);
			const double disc = b * b - 4.0 * a * c;
			if (disc >= 0) {
				const double t = (-b - std::sqrt(disc)) / (2.0 * a);
				if (t >= 0.0 && t <= 1.0) q = a_c0 + D * t;
				else q = a_c0 + D * std::clamp(-Dot(f, D) / a, 0.0, 1.0);
			} else {
				q = a_c0 + D * std::clamp(-Dot(f, D) / a, 0.0, 1.0);
			}
		} else if (a > 1e-9) {
			// touching at the start of the frame: from where he was then
			q = a_c0;
		}
		const Vec3 d = a_p - q;
		if (LengthSq(d) < 1e-6) return fb;
		return Normalize(d, fb);
	}

	Impulse Contact(const Vec3& a_dir, const Vec3& a_vHis, double a_mHis, const Vec3& a_vBody, double a_mBody, double a_minVn, double a_maxDv)
	{
		Impulse o;
		o.dir = a_dir;
		if (!IsFinite(a_dir) || !IsFinite(a_vHis) || !IsFinite(a_vBody) || !(a_mHis > 0) || !(a_mBody > 0) || !std::isfinite(a_mHis) || !std::isfinite(a_mBody)) return o;
		o.vn = Dot(a_vHis - a_vBody, a_dir);
		if (!(o.vn >= a_minVn) || !(o.vn > 0)) return o;
		const double dvRaw = o.vn * a_mHis / (a_mHis + a_mBody);
		o.dv = std::min(dvRaw, std::max(0.0, a_maxDv));
		o.capped = o.dv < dvRaw;
		if (!(o.dv > 0)) return o;
		o.J = a_dir * (a_mBody * o.dv);
		o.ok = IsFinite(o.J);
		return o;
	}

	Vec3 AwayFromFriends(const Vec3& a_dir, const Vec3& a_p, std::span<const Friend> a_friends, double a_deg, double a_r, bool* a_turned, bool a_recheck)
	{
		if (a_turned) *a_turned = false;
		Vec3 d = a_dir;
		if (!(a_deg > 0)) return d;
		bool turnedAny = false;
		for (const auto& f : a_friends) {
			const Vec3 to = Flat(f.pos - a_p);
			const double lt = Length(to);
			if (lt < 1.0 || Length(f.pos - a_p) > a_r) continue;
			const Vec3   fd = Flat(d);
			const double lf = Length(fd);
			if (lf < 1e-6) continue;  // straight up / down: never toward anyone flat
			const double ang = AngleDeg(fd, to);
			if (ang >= a_deg) continue;
			// turn the flat part about z to a_deg off the line toward the friend, on the side it already leaned to
			const double side = Cross(to, fd).z >= 0 ? 1.0 : -1.0;
			const double h = std::atan2(to.y, to.x) + side * a_deg * kDeg2Rad;
			const Vec3   nf{ std::cos(h) * lf, std::sin(h) * lf, 0.0 };
			d = Normalize(Vec3{ nf.x, nf.y, d.z }, d);
			turnedAny = true;
			if (a_turned) *a_turned = true;
		}
		// B2 codex review (SHV-03, shove.friendRecheck): each turn above only looks at its own friend, so a later one can turn the push
		// back toward an earlier one (the player first: player 0 deg + a friend at 100 deg -> 60 deg, then 40 deg off the player).
		// The final direction must clear every friend; if it cannot, no shove ({}: Contact refuses a zero direction).
		if (a_recheck && turnedAny) {
			const Vec3 fd = Flat(d);
			if (Length(fd) >= 1e-6)
				for (const auto& f : a_friends) {
					const Vec3 to = Flat(f.pos - a_p);
					if (Length(to) < 1.0 || Length(f.pos - a_p) > a_r) continue;
					if (AngleDeg(fd, to) < a_deg - 1e-7) return {};
				}
		}
		return d;
	}

	// ------------------------------------------------------------------ path A
	void Shover::Reset()
	{
		_cool = {};
		_coolNext = 0;
		_nearHave = false;
		_ng = 0;
		_nw = _frameWrites = _frameQueries = 0;
		_reactionRaw = _reactionSent = {};
		_workAt = -1e9;
		_restoreAll = _np > 0;  // Reset is also used without a world change; do not abandon our filter writes.
	}
	bool Shover::IsPassed(std::uint32_t a_body) const
	{
		for (int i = 0; i < _np; ++i)
			if (_pass[static_cast<std::size_t>(i)].body == a_body) return true;
		return false;
	}
	bool Shover::Cooling(std::uint32_t a_body, double a_now, double a_cd) const
	{
		for (const auto& c : _cool)
			if (c.body == a_body && a_now - c.at < a_cd && a_now >= c.at) return true;
		return false;
	}
	void Shover::NoteCool(std::uint32_t a_body, double a_now)
	{
		for (auto& c : _cool)
			if (c.body == a_body) {
				c.at = a_now;
				return;
			}
		_cool[static_cast<std::size_t>(_coolNext)] = { a_body, a_now };
		_coolNext = (_coolNext + 1) % static_cast<int>(_cool.size());
	}


	void Shover::BeginWork(double a_now)
	{
		if (_workAt == a_now) return;
		_workAt = a_now;
		_nw = _frameWrites = _frameQueries = 0;
		_reactionRaw = _reactionSent = {};
	}
	bool Shover::WrittenMotion(std::uint32_t a_body, const BodyInfo& a_info) const
	{
		for (int i = 0; i < _nw; ++i) {
			const auto& w = _written[static_cast<std::size_t>(i)];
			if (w.body == a_body || (a_info.motion != 0 && w.motion == a_info.motion)) return true;
			if (a_info.motion == 0 && w.motion == 0 && LengthSq(a_info.com) > 1.0 && LengthSq(w.com) > 1.0 && LengthSq(a_info.com - w.com) < 1.0) return true;
		}
		return false;
	}
	void Shover::NoteWritten(std::uint32_t a_body, const BodyInfo& a_info)
	{
		if (_nw < static_cast<int>(_written.size())) _written[static_cast<std::size_t>(_nw++)] = { a_body, a_info.motion, a_info.com };
		++_frameWrites;
		++_st.impulseWrites;
	}
	namespace
	{
		// The same final-vector bound is used by both contact producers. An already over-cap body may only lose speed.
		Impulse SafeContact(const Vec3& dir, const Vec3& vHis, double mHis, const BodyInfo& info, Who who, const Cfg& c)
		{
			if (!info.ok || !info.dynamic || info.keyframed || !(info.invMass > 0) || !std::isfinite(info.invMass) || !IsFinite(info.vel)) return {};
			const double limit = (who == Who::kCorpse ? c.maxDvCorpse : who == Who::kFoe ? c.maxDvFoe : c.maxDvProp) * kToGame;
			if (!(limit > 0) || Dot(info.vel, dir) >= limit) return {};
			double dvCap = limit;
			if (c.capFinal || (who == Who::kProp && c.govV > 0)) {
				const double cap = who == Who::kProp && c.govV > 0 ? std::min(limit, c.govV) : limit;
				const double along = Dot(info.vel, dir), tangent2 = std::max(0.0, LengthSq(info.vel) - along * along);
				dvCap = std::min(limit, std::max(0.0, std::sqrt(std::max(0.0, cap * cap - tangent2)) - along));
			}
			const double minApproach = std::max(c.minVn * kToGame, c.minPartV);
			auto imp = Contact(dir, vHis, mHis, info.vel, 1.0 / info.invMass, minApproach, dvCap);
			if (imp.ok && (c.capFinal || (who == Who::kProp && c.govV > 0))) {
				const double cap = who == Who::kProp && c.govV > 0 ? std::min(limit, c.govV) : limit;
				if (Length(info.vel + imp.J * info.invMass) > std::max(cap, Length(info.vel)) + 1e-6) return {};
			}
			return imp;
		}
		void Reactions(FrameOut& o, const FrameIn& in, const Cfg& c, const std::array<Vec3, B::kPartCount>& reactJ, std::array<Vec3, B::kPartCount>& raw, std::array<Vec3, B::kPartCount>& sent)
		{
			for (int part = 0; part < B::kPartCount; ++part) {
				const auto k = static_cast<std::size_t>(part);
				raw[k] += reactJ[k];
				Vec3 J = raw[k];
				const double m = in.partMass ? in.partMass[part] : 0.0;
				if (!(m > 0) || (LengthSq(J) <= 0 && LengthSq(sent[k]) <= 0) || o.nReact >= static_cast<int>(o.react.size())) continue;
				const double dv = Length(J) / m;
				if (dv > c.reactMaxDv && dv > 0) J = J * (c.reactMaxDv / dv);
				const Vec3 delta=J-sent[k];sent[k]=J;
				if(LengthSq(delta)>0)o.react[static_cast<std::size_t>(o.nReact++)] = { part, delta };
			}
		}
		struct CostReceipt
		{
			FrameOut& out; Stats& stats; IWorld& world; bool whole;
			FrameOut Done() { out.us = world.Us(); return out; }
			~CostReceipt()
			{
				out.us = world.Us();
				if (whole) { stats.wholeUsLast = out.us; stats.wholeUsMax = std::max(stats.wholeUsMax, out.us); }
				else { stats.usLast = out.us; stats.usMax = std::max(stats.usMax, out.us); }
			}
		};
	}

	FrameOut Shover::Frame(const FrameIn& a_in, const Cfg& a_c, IWorld& a_w, bool a_probeOnly)
	{
		FrameOut o;
		CostReceipt receipt{ o, _st, a_w, false };
		BeginWork(a_in.now);
		++_st.frames;
		if ((!a_c.on && !a_probeOnly) || a_in.maintenanceOnly) { ++_st.noEngine; return receipt.Done(); }
		std::array<PartSweep, B::kPartCount> parts{};
		const int np = MergeParts(a_in.shapes, a_in.partMass, a_c.massGain, a_in.totalMass, parts.data(), static_cast<int>(parts.size()));
		if (np <= 0) { ++_st.noEngine; return receipt.Done(); }
		Vec3 lo{ 1e18, 1e18, 1e18 }, hi{ -1e18, -1e18, -1e18 };
		for (int i = 0; i < np; ++i)
			for (const Vec3& p : { parts[static_cast<std::size_t>(i)].c0, parts[static_cast<std::size_t>(i)].c1 }) {
				const double r = parts[static_cast<std::size_t>(i)].R;
				lo = { std::min(lo.x, p.x-r), std::min(lo.y, p.y-r), std::min(lo.z, p.z-r) };
				hi = { std::max(hi.x, p.x+r), std::max(hi.y, p.y+r), std::max(hi.z, p.z+r) };
			}
		const std::uint64_t mask = (a_c.corpses ? kLayCorpse : 0) | (a_c.props ? kLayProps : 0) | (a_c.foes ? kLayFoe : 0);
		const std::uint64_t wideMask = mask | kLayProps | (a_c.pCorpses ? kLayCorpse : 0);
		const double pad = std::max(a_c.nearPad, a_c.pushers ? a_c.pNearR : 0.0) + a_c.reach;
		const Vec3 vp{ pad, pad, pad }, qlo=lo-vp, qhi=hi+vp;
		// Only a positive pusher-lifetime hint is reusable. Every path-A negative and contact admission is refreshed:
		// another body can enter a stationary query box, even when his own geometry and IDs have not changed.
		const bool reuse = a_c.nearEvery > 0 && _nearHave && a_in.now >= _nearAt && a_in.now-_nearAt < a_c.nearEvery && wideMask == _nearMask &&
			qlo.x >= _nearLo.x && qlo.y >= _nearLo.y && qlo.z >= _nearLo.z && qhi.x <= _nearHi.x && qhi.y <= _nearHi.y && qhi.z <= _nearHi.z;
		if(reuse && (a_probeOnly || !a_c.on)){o.propsNear=true;++_st.nearReused;++_st.noEngine;return receipt.Done();}
		const bool reserveCapsule = a_c.pass && a_in.capOk && Length(a_in.capVel) >= a_c.passV;
		const int queryLimit = reserveCapsule ? std::max(0, a_c.maxQueries - 3) : a_c.maxQueries;
		const double timeLimit = reserveCapsule ? a_c.budgetUs * 0.25 : a_c.budgetUs;
		auto queryRoom = [&] { return _frameQueries < queryLimit && a_w.Us() < timeLimit; };
		const bool mandatoryProbe=a_c.pushers && !reuse;
		if (!queryRoom() && !mandatoryProbe) { ++_st.governor; ++_st.noEngine; return receipt.Done(); }
		if(mandatoryProbe){++_st.mandatoryProbes;if(!queryRoom())++_st.mandatoryOverBudget;}
		if (!a_w.Begin()) return receipt.Done();
		bool near = true;
		if (reuse) ++_st.nearReused;
		else {
			near = a_w.AnyNear(qlo,qhi,wideMask); ++o.workQueries; ++_frameQueries; ++_st.workQueries; ++_st.nearProbes;
			_nearHave = near; _nearAt=a_in.now; _nearLo=qlo; _nearHi=qhi; _nearMask=wideMask;
		}
		o.propsNear = near;
		if (a_probeOnly || !a_c.on || !near || mask == 0) { if (!near) ++_st.culled; a_w.End(); return receipt.Done(); }
		// A reused positive hint still requires a fresh tight query, even when wide/tight pads coincide.
		if (reuse || pad > a_c.nearPad+a_c.reach+1e-9) {
			if (!queryRoom()) { ++_st.governor; a_w.End(); return receipt.Done(); }
			const double p=a_c.nearPad+a_c.reach; const Vec3 v{p,p,p};
			const bool tight=a_w.AnyNear(lo-v,hi+v,mask); ++o.workQueries; ++_frameQueries; ++_st.workQueries;
			if (!tight) { ++_st.culled; a_w.End(); return receipt.Done(); }
		}
		struct Cand { BodyHit h{}; int part{-1}; BodyInfo info{}; std::uint32_t filter{0}; std::uintptr_t shape{0}; bool filtered{false}; };
		std::array<Cand,32> cand{}; int nc=0;
		for (int i=0;i<np;++i) {
			if (!queryRoom()) { ++_st.governor; break; }
			const auto& p=parts[static_cast<std::size_t>(i)]; std::array<BodyHit,8> hb{};
			const int k=a_w.Closest(p.c0,p.c1,p.R,a_c.reach,mask,hb.data(),static_cast<int>(hb.size()));
			++o.queries; ++o.workQueries; ++_frameQueries; ++_st.queries; ++_st.workQueries;
			if (k<0) { o.fault=true; ++_st.faults; break; }
			for (int j=0;j<std::min(k,static_cast<int>(hb.size()));++j) {
				++o.hits; ++_st.hits; bool dup=false;
				for(int m=0;m<nc;++m) dup=dup || cand[static_cast<std::size_t>(m)].h.body==hb[static_cast<std::size_t>(j)].body;
				if (dup || nc>=static_cast<int>(cand.size())) continue;
				auto& c=cand[static_cast<std::size_t>(nc++)];c.h=hb[static_cast<std::size_t>(j)];c.part=i;
				c.info=a_w.Info(c.h.body);c.filtered=a_w.Filter(c.h.body,c.filter,c.shape);
				if(a_c.govV>0 && a_in.pushersSolid && o.nWatched<static_cast<int>(o.watched.size())) {
					FrameIn admission=a_in;admission.pushersSolid=false;
					if(Classify(c.h,c.info,admission,a_c,nullptr)==Who::kProp) o.watched[static_cast<std::size_t>(o.nWatched++)]={c.h.body,Who::kProp,0,c.h.p,c.info.motion,c.filter,c.shape};
				}
			}
		}
		a_w.End();
		if(o.fault || nc==0) return receipt.Done();
		if(a_w.Us()>=a_c.budgetUs) { ++_st.governor; return receipt.Done(); }
		if(!a_w.BeginWrite()) { ++_st.writeBusy; return receipt.Done(); }
		std::array<Vec3,B::kPartCount> reactJ{};
		for(int m=0;m<nc;++m) {
			if(_frameWrites>=a_c.maxPushes || a_w.Us()>=a_c.budgetUs) { ++_st.governor; break; }
			const auto& c=cand[static_cast<std::size_t>(m)];
			const auto info=a_w.Info(c.h.body);BodyHit hit=c.h;std::uint32_t filter=0;std::uintptr_t shape=0;
			const bool filtered=a_w.Filter(hit.body,filter,shape);
			if(!c.filtered || !filtered || shape==0 || !IsFinite(hit.p) || !IsFinite(hit.n) || shape!=c.shape || filter!=c.filter || (c.info.motion!=0 && info.motion!=c.info.motion)) { ++_st.recheckSkips; continue; }
			if(filtered) { hit.layer=filter&0x7Fu; hit.group=filter>>16; if(filter&kNoCollideBit) {++_st.recheckSkips;continue;} }
			// Move the cached surface point with its current motion before clearance/direction checks.
			if(LengthSq(c.info.com)>1.0 && IsFinite(info.com)) hit.p+=info.com-c.info.com;
			const Who who=Classify(hit,info,a_in,a_c,nullptr);
			switch(who) {
			case Who::kFriend:++_st.friendSkips;break; case Who::kNearFriend:++_st.nearFriendSkips;break;
			case Who::kStatic:++_st.staticSkips;break; case Who::kLiving:++_st.livingSkips;break;
			case Who::kSkip:++_st.skipGroup;break; case Who::kSelf:++_st.selfSkips;break; case Who::kPusher:++_st.pusherSkips;break;default:break;
			}
			if(!Pushable(who)) continue;
			if(Cooling(hit.body,a_in.now,a_c.cooldown) || WrittenMotion(hit.body,info)) {++_st.coolSkips;continue;}
			const auto& p=parts[static_cast<std::size_t>(c.part)];bool turned=false;
			if(a_c.slowSkip && Length(p.vel-info.vel)<a_c.minVn*kToGame){++_st.slowParts;continue;}
			Vec3 dir=PushDir(p.c0,p.c1,p.R+a_c.reach,hit.p,hit.n);
			dir=AwayFromFriends(dir,hit.p,a_in.friends,a_c.awayDeg,a_c.awayR,&turned,a_c.friendRecheck);
			const auto imp=SafeContact(dir,p.vel,p.mass,info,who,a_c);
			if(!imp.ok) {++_st.sepSkips;if(a_c.minPartV>0 && Dot(p.vel-info.vel,dir)<a_c.minPartV)++_st.slowParts;continue;}
			const bool ok=who==Who::kFoe?a_w.LinearImpulse(hit.body,imp.J):a_w.Impulse(hit.body,imp.J,hit.p);
			if(!ok){o.fault=true;++_st.faults;break;}
			NoteCool(hit.body,a_in.now);NoteWritten(hit.body,info);++o.impulseWrites;
			if(o.nPushed<static_cast<int>(o.pushed.size())) o.pushed[static_cast<std::size_t>(o.nPushed++)]={hit.body,who,imp.dv,hit.p,info.motion,filter,shape};
			++o.pushes;++_st.pushes;if(who==Who::kCorpse)++_st.pushCorpse;else if(who==Who::kProp)++_st.pushProp;else ++_st.pushFoe;
			if(imp.capped)++_st.capped;if(turned)++_st.turned;_st.dvMax=std::max(_st.dvMax,imp.dv);
			if(p.part>=0 && p.part<B::kPartCount)reactJ[static_cast<std::size_t>(p.part)]-=imp.J*a_c.react;
		}
		a_w.EndWrite();Reactions(o,a_in,a_c,reactJ,_reactionRaw,_reactionSent);return receipt.Done();
	}

	// ------------------------------------------------------------------ B2 lane 3: the capsule pass + the speed governor
	Vec3 GovernImpulse(const Vec3& a_v, double a_m, double a_maxV)
	{
		const double s = Length(a_v);
		if (!(a_maxV > 0) || !(a_m > 0) || !std::isfinite(a_m) || !IsFinite(a_v) || !(s > a_maxV)) return {};
		return a_v * (-a_m * (s - a_maxV) / s);
	}

	namespace
	{
		double DistPointSeg(const Vec3& p, const Vec3& a, const Vec3& b)
		{
			const Vec3   d = b - a;
			const double L2 = LengthSq(d);
			const double t = L2 > 1e-12 ? std::clamp(Dot(p - a, d) / L2, 0.0, 1.0) : 0.0;
			return Length(p - (a + d * t));
		}
		// the distance from p to the parallelogram {a + t U + s D | t, s in [0, 1]} (a vertical capsule axis swept along D)
		double DistPointSweep(const Vec3& p, const Vec3& a, const Vec3& U, const Vec3& D)
		{
			const double uu = Dot(U, U), dd = Dot(D, D), ud = Dot(U, D);
			const double det = uu * dd - ud * ud;
			const Vec3   w = p - a;
			if (det > 1e-9 * (uu * dd + 1e-12)) {
				const double wu = Dot(w, U), wd = Dot(w, D);
				const double t = (wu * dd - wd * ud) / det, s = (wd * uu - wu * ud) / det;
				if (t >= 0 && t <= 1 && s >= 0 && s <= 1) return Length(w - U * t - D * s);
			}
			double m = DistPointSeg(p, a, a + U);
			m = std::min(m, DistPointSeg(p, a + D, a + D + U));
			m = std::min(m, DistPointSeg(p, a, a + D));
			m = std::min(m, DistPointSeg(p, a + U, a + U + D));
			return m;
		}
		void CapsuleAxis(const Vec3& a_pos, double a_r, double a_h, Vec3& a_a, Vec3& a_b)
		{
			const double r = std::max(0.0, a_r), h = std::max(a_h, 2.0 * r);
			a_a = a_pos + Vec3{ 0, 0, r };
			a_b = a_pos + Vec3{ 0, 0, h - r };
		}
	}

	bool CapsuleSweepTouches(const Vec3& a_pos, const Vec3& a_vel, double a_r, double a_h, double a_look, const Vec3& a_p, double a_pad)
	{
		if (!IsFinite(a_pos) || !IsFinite(a_vel) || !IsFinite(a_p) || !(a_r > 0) || !(a_h > 0)) return false;
		Vec3 a, b;
		CapsuleAxis(a_pos, a_r, a_h, a, b);
		return DistPointSweep(a_p, a, b - a, a_vel * std::max(0.0, a_look)) <= a_r + std::max(0.0, a_pad);
	}

	double CapsuleGap(const Vec3& a_pos, double a_r, double a_h, const Vec3& a_p)
	{
		if (!IsFinite(a_pos) || !IsFinite(a_p) || !(a_r > 0) || !(a_h > 0)) return 1e9;
		Vec3 a, b;
		CapsuleAxis(a_pos, a_r, a_h, a, b);
		return DistPointSeg(a_p, a, b) - a_r;
	}


	FrameOut Shover::Capsule(const FrameIn& a_in, const Cfg& a_c, IWorld& a_w, const FrameOut& a_pushed)
	{
		FrameOut o;
		CostReceipt receipt{ o, _st, a_w, true };
		BeginWork(a_in.now);
		const double now=a_in.now;
		const bool capOk=a_in.capOk && IsFinite(a_in.capPos) && IsFinite(a_in.capVel) && a_in.capR>0 && a_in.capH>0;
		const bool force=a_in.maintenanceOnly || _restoreAll || !a_c.pass;
		const bool fast=!force && capOk && a_in.selfGroup!=0 && Length(a_in.capVel)>=a_c.passV;
		const double look=std::max(a_c.passLook,1.5*std::max(a_in.dt,0.0));
		const double look1=std::min(look,std::max(1.0/90.0,1.5*std::max(a_in.dt,0.0)));
		for(int i=0;i<_ng;) {
			const auto& g=_gov[static_cast<std::size_t>(i)];
			if(a_in.maintenanceOnly || _restoreAll || a_c.govV<=0 || now<g.at || now-g.at>1.0) _gov[static_cast<std::size_t>(i)]=_gov[static_cast<std::size_t>(--_ng)];
			else ++i;
		}
		if(!fast && _np==0 && _ng==0 && (a_c.govV<=0 || (a_pushed.nPushed==0 && a_pushed.nWatched==0) || a_in.maintenanceOnly)) return receipt.Done();
		if(fast)++_st.passFrames;
		auto room=[&]{return _frameQueries<a_c.maxQueries && a_w.Us()<a_c.budgetUs;};
		auto counted=[&]{++o.workQueries;++_frameQueries;++_st.workQueries;++_st.passQueries;};
		struct Cand {BodyHit h{};BodyInfo info{};std::uint32_t filter{0};std::uintptr_t shape{0};bool now{false};};
		std::array<Cand,16> cand{};int nc=0;
		// Optional discovery is bounded together with Frame. Pending filter retirement below does not require a read section.
		if(fast && _np<std::clamp(a_c.passMax,0,64) && room() && a_w.Begin()) {
			const Vec3 p1=a_in.capPos+a_in.capVel*look;
			const double hh=0.5*std::max(a_in.capH,2.0*a_in.capR);const Vec3 up{0,0,hh};
			std::array<BodyHit,16> hb{};
			const int n=a_w.Closest(a_in.capPos+up,p1+up,hh+a_c.passPad,0.0,kLayCapsuleJunk,hb.data(),static_cast<int>(hb.size()));
			counted();++o.queries;
			if(n<0){o.fault=true;++_st.passFaults;}
			for(int j=0;j<std::min(n,static_cast<int>(hb.size())) && room();++j) {
				const auto& h=hb[static_cast<std::size_t>(j)];
				if(IsPassed(h.body) || h.layer>=64 || !((kLayCapsuleJunk>>h.layer)&1ull) || !CapsuleSweepTouches(a_in.capPos,a_in.capVel,a_in.capR,a_in.capH,look,h.p,a_c.passPad))continue;
				bool dup=false;for(int k=0;k<nc;++k)dup=dup || cand[static_cast<std::size_t>(k)].h.body==h.body;
				if(dup)continue;
				Cand c;c.h=h;c.info=a_w.Info(h.body);
				if(!IsFinite(c.h.p) || !IsFinite(c.h.n) || !c.info.ok || !c.info.dynamic || c.info.keyframed || !(c.info.invMass>0) || !std::isfinite(c.info.invMass) || !a_w.Filter(h.body,c.filter,c.shape))continue;
				if(c.shape==0 || (c.filter&kNoCollideBit) || ((c.filter>>16)==0 && c.info.motion==0) || (c.filter>>16)==a_in.selfGroup)continue;
				c.h.layer=c.filter&0x7Fu;c.h.group=c.filter>>16;
				const Who who=Classify(c.h,c.info,a_in,a_c,nullptr);
				if(who!=Who::kProp && who!=Who::kNearFriend){++_st.passSkipFriend;continue;}
				// A saturated neighborhood is incomplete evidence: never split a compound object's collision group.
				std::array<BodyHit,16> nb{};
				const int kn=a_w.Closest(h.p,h.p,96.0,0.0,kLayProps,nb.data(),static_cast<int>(nb.size()));counted();++o.queries;
				bool multi=kn<0 || kn>=static_cast<int>(nb.size());
				for(int k=0;k<std::min(kn,static_cast<int>(nb.size()));++k) {
					const auto& other=nb[static_cast<std::size_t>(k)];
					multi=multi || (other.body!=h.body && c.h.group!=0 && other.group==c.h.group);
					if(other.body!=h.body && c.info.motion!=0)multi=multi || a_w.Info(other.body).motion==c.info.motion;
				}
				if(multi){++_st.passMulti;continue;}
				c.now=CapsuleSweepTouches(a_in.capPos,a_in.capVel,a_in.capR,a_in.capH,look1,h.p,a_c.passPad);
				if(nc<static_cast<int>(cand.size()))cand[static_cast<std::size_t>(nc++)]=c;
			}
			a_w.End();
		}
		if(_np==0 && _ng==0 && nc==0 && (a_c.govV<=0 || (a_pushed.nPushed==0 && a_pushed.nWatched==0) || a_in.maintenanceOnly))return receipt.Done();
		if(!a_w.BeginWrite()){++_st.passBusy;return receipt.Done();}
		std::array<Vec3,B::kPartCount> reactJ{};
		const double mHis=[&]{double m=a_in.partMass?(a_in.partMass[B::kHip]+a_in.partMass[B::kTorso])*a_c.massGain:60.0;if(a_in.totalMass>0)m=std::min(m,a_in.totalMass);return m>0?m:60.0;}();
		const Vec3 c0=a_in.capPos+Vec3{0,0,0.5*a_in.capH},c1=c0+a_in.capVel*look1;
		auto writeRoom=[&]{return _frameWrites<a_c.maxPushes && a_w.Us()<a_c.budgetUs;};
		auto bump=[&](const BodyHit& h,const BodyInfo& info,std::uint32_t filter,std::uintptr_t shape)->int{
			// All producers use CURRENT category, eligibility, clearance, direction, motion identity, and vector cap.
			if(Classify(h,info,a_in,a_c,nullptr)!=Who::kProp){++_st.passSkipFriend;return 0;}
			if(!writeRoom() || Cooling(h.body,now,a_c.cooldown) || WrittenMotion(h.body,info))return 0;
			if(a_c.slowSkip && Length(a_in.capVel-info.vel)<a_c.minVn*kToGame){++_st.slowParts;return 0;}
			bool turned=false;Vec3 dir=PushDir(c0,c1,a_in.capR+a_c.passPad,h.p,h.n);
			dir=AwayFromFriends(dir,h.p,a_in.friends,a_c.awayDeg,a_c.awayR,&turned,a_c.friendRecheck);
			const auto imp=SafeContact(dir,a_in.capVel,mHis,info,Who::kProp,a_c);if(!imp.ok)return 0;
			if(!a_w.Impulse(h.body,imp.J,h.p)){++_st.passFaults;o.fault=true;return -1;}
			NoteCool(h.body,now);NoteWritten(h.body,info);++o.impulseWrites;++o.pushes;++_st.passPushes;++_st.pushes;++_st.pushProp;
			if(imp.capped)++_st.capped;if(turned)++_st.turned;_st.dvMax=std::max(_st.dvMax,imp.dv);
			reactJ[static_cast<std::size_t>(B::kHip)]-=imp.J*a_c.react;
			if(o.nPushed<static_cast<int>(o.pushed.size()))o.pushed[static_cast<std::size_t>(o.nPushed++)]={h.body,Who::kProp,imp.dv,h.p,info.motion,filter,shape};
			return 1;
		};
		auto govern=[&](const BodyHit& h,const BodyInfo& info)->bool{
			FrameIn admission=a_in;admission.pushersSolid=false;
			if(a_c.govV<=0 || !writeRoom() || !IsFinite(h.p) || Classify(h,info,admission,a_c,nullptr)!=Who::kProp || WrittenMotion(h.body,info))return true;
			const Vec3 J=GovernImpulse(info.vel,1.0/info.invMass,a_c.govV);if(LengthSq(J)<=0)return true;
			if(!a_w.LinearImpulse(h.body,J)){++_st.passFaults;o.fault=true;return false;}
			NoteWritten(h.body,info);++o.impulseWrites;++_st.governed;_st.governedVMax=std::max(_st.governedVMax,Length(info.vel));return true;
		};
		// Filter restoration has priority, including when all feature keys are off, paused, or Reset was called.
		for(int i=0;i<_np;) {
			auto& e=_pass[static_cast<std::size_t>(i)];std::uint32_t f=0;std::uintptr_t shape=0;
			if(!a_w.Filter(e.body,f,shape)){if(a_w.BodyGone(e.body)){++_st.passLost;_pass[static_cast<std::size_t>(i)]=_pass[static_cast<std::size_t>(--_np)];continue;}++_st.passFaults;++i;continue;}  // transient read failure retains ownership for retry
			if(f!=e.set || shape!=e.shape){++_st.passLost;_pass[static_cast<std::size_t>(i)]=_pass[static_cast<std::size_t>(--_np)];continue;}
			const auto info=a_w.Info(e.body);
			const bool motionChanged=e.motion!=0 && info.ok && info.motion!=e.motion;
			const double age=now-e.at;
			const bool clear=capOk && info.ok && CapsuleGap(a_in.capPos,a_in.capR,a_in.capH,info.com)>a_c.passClear &&
				!(fast && CapsuleSweepTouches(a_in.capPos,a_in.capVel,a_in.capR,a_in.capH,look,info.com,a_c.passClear));
			const bool restore=force || motionChanged || !capOk || age<0 || (age>=a_c.passHold && (clear || (!fast && age>=a_c.passHoldMax) || age>=10.0));
			if(restore){
				if(a_w.SetFilter(e.body,e.orig)){++_st.passRestored;_pass[static_cast<std::size_t>(i)]=_pass[static_cast<std::size_t>(--_np)];continue;}
				++_st.passFaults;o.fault=true;++i;continue;
			}
			if(!a_in.maintenanceOnly && info.ok){
				BodyHit h{e.body,e.orig&0x7Fu,e.orig>>16,info.com+e.off,e.n,0};
				if(fast && !e.bumped && CapsuleSweepTouches(a_in.capPos,a_in.capVel,a_in.capR,a_in.capH,look1,h.p,a_c.passPad)){
					const int r=bump(h,info,e.orig,e.shape);if(r>0)e.bumped=true;if(r==0)++_st.passNoPush;
				}else if(!force)govern(h,info);
			}
			++i;
		}
		if(_np==0)_restoreAll=false;
		// Register freshly pushed bodies with an identity snapshot; later governor writes require that exact body/category.
		if(!a_in.maintenanceOnly && a_c.govV>0)for(int i=0;i<a_pushed.nPushed+a_pushed.nWatched && i<static_cast<int>(a_pushed.pushed.size()+a_pushed.watched.size());++i){
			const auto& p=i<a_pushed.nPushed?a_pushed.pushed[static_cast<std::size_t>(i)]:a_pushed.watched[static_cast<std::size_t>(i-a_pushed.nPushed)];if(p.who!=Who::kProp)continue;
			std::uint32_t f=0;std::uintptr_t shape=0;const auto info=a_w.Info(p.body);
			if(!a_w.Filter(p.body,f,shape) || !info.ok || (p.shape!=0 && p.shape!=shape) || (p.motion!=0 && p.motion!=info.motion))continue;
			int k=0;while(k<_ng && _gov[static_cast<std::size_t>(k)].body!=p.body)++k;
			if(k==_ng){if(_ng>=static_cast<int>(_gov.size()))continue;++_ng;}
			_gov[static_cast<std::size_t>(k)]={p.body,now,f,info.motion,shape};
		}
		for(int i=0;i<_ng && !a_in.maintenanceOnly && a_c.govV>0;){
			const auto g=_gov[static_cast<std::size_t>(i)];if(IsPassed(g.body)){++i;continue;}
			std::uint32_t f=0;std::uintptr_t shape=0;const auto info=a_w.Info(g.body);
			if(!a_w.Filter(g.body,f,shape) || !info.ok){++i;continue;}
			BodyHit h{g.body,f&0x7Fu,f>>16,info.com,{},0};
			FrameIn admission=a_in;admission.pushersSolid=false;
			if(f!=g.filter || shape!=g.shape || (g.motion!=0 && info.motion!=g.motion) || Classify(h,info,admission,a_c,nullptr)!=Who::kProp){_gov[static_cast<std::size_t>(i)]=_gov[static_cast<std::size_t>(--_ng)];continue;}
			if(!govern(h,info))break;++i;
		}
		for(int i=0;i<nc && !o.fault && !force;++i){
			if(a_w.Us()>=a_c.budgetUs) {++_st.governor;break;}
			const auto& c=cand[static_cast<std::size_t>(i)];
			if(_np>=std::clamp(a_c.passMax,0,64) || _np>=static_cast<int>(_pass.size())){++_st.passFull;break;}
			std::uint32_t f=0;std::uintptr_t shape=0;const auto info=a_w.Info(c.h.body);
			if(!a_w.Filter(c.h.body,f,shape) || f!=c.filter || shape!=c.shape || (c.info.motion!=0 && info.motion!=c.info.motion)){++_st.recheckSkips;continue;}
			BodyHit h=c.h;h.layer=f&0x7Fu;h.group=f>>16;if(IsFinite(info.com) && IsFinite(c.info.com))h.p+=info.com-c.info.com;
			const Who who=Classify(h,info,a_in,a_c,nullptr);
			if((who!=Who::kProp && who!=Who::kNearFriend) || !IsFinite(info.com) || (f&kNoCollideBit)){++_st.passSkipFriend;continue;}
			const std::uint32_t set=(a_in.selfGroup<<16)|(f&0xFFFFu);
			if(!a_w.SetFilter(h.body,set)){++_st.passFaults;o.fault=true;break;}
			Passed e;e.body=h.body;e.orig=f;e.set=set;e.shape=shape;e.at=now;e.motion=info.motion;e.off=h.p-info.com;e.n=h.n;e.bumpable=who==Who::kProp;
			_pass[static_cast<std::size_t>(_np++)]=e;++_st.passed;
			if(c.now && e.bumpable){const int r=bump(h,info,f,shape);if(r>0)_pass[static_cast<std::size_t>(_np-1)].bumped=true;if(r==0)++_st.passNoPush;}
		}
		a_w.EndWrite();Reactions(o,a_in,a_c,reactJ,_reactionRaw,_reactionSent);return receipt.Done();
	}

	std::string Shover::Describe() const
	{
		const auto& s = _st;
		return std::format("frames={} culled={} q={} hits={} pushes={} corpse={} prop={} foe={} capped={} turned={} dvMax={:.0f} skip friend={} nearFriend={} static={} living={} "
						   "sep={} cool={} pusher={} group={} self={} over={} busy={} gov={} faults={} usLast={:.0f} usMax={:.0f} FRIEND_IMPULSES={} "
						   "slowParts={} victimSkips={} passFrames={} passQ={} passed={} passPushes={} passNoPush={} passRestored={} passLost={} passBusy={} passFull={} passSkip={} "
						   "passMulti={} passFaults={} passForgot={} passNow={} governed={} governedVMax={:.0f} workQ={} writes={} recheck={} wholeUs={:.0f}/{:.0f} probes={} reused={} noEngine={} mandatoryProbe={} mandatoryOverBudget={}",
			s.frames, s.culled, s.queries, s.hits, s.pushes, s.pushCorpse, s.pushProp, s.pushFoe, s.capped, s.turned, s.dvMax, s.friendSkips, s.nearFriendSkips, s.staticSkips,
			s.livingSkips, s.sepSkips, s.coolSkips, s.pusherSkips, s.skipGroup, s.selfSkips, s.overPush, s.writeBusy, s.governor, s.faults, s.usLast, s.usMax, s.friendImpulses,
			s.slowParts, s.victimSkips, s.passFrames, s.passQueries, s.passed, s.passPushes, s.passNoPush, s.passRestored, s.passLost, s.passBusy, s.passFull, s.passSkipFriend,
			s.passMulti, s.passFaults, s.passForgot, _np, s.governed, s.governedVMax, s.workQueries, s.impulseWrites, s.recheckSkips, s.wholeUsLast, s.wholeUsMax, s.nearProbes, s.nearReused, s.noEngine, s.mandatoryProbes, s.mandatoryOverBudget);
	}

	// ------------------------------------------------------------------ path B
	int ChoosePushers(std::span<const PartShape> a_shapes, int a_max, PusherSpec* a_out, int a_cap)
	{
		int n = 0;
		for (const int part : kPusherOrder) {
			if (n >= a_max || n >= a_cap) break;
			const PartShape* best = nullptr;
			double           bestB = -1;
			for (const auto& s : a_shapes) {
				if (s.part != part) continue;
				const double b = 0.5 * Length(s.b1 - s.a1) + s.r;
				if (b > bestB) {
					bestB = b;
					best = &s;
				}
			}
			if (!best || !(best->r > 0.5)) continue;
			PusherSpec& p = a_out[n++];
			p.part = part;
			p.r = best->r;
			p.hh = best->kind == 1 ? 0.5 * Length(best->b1 - best->a1) : 0.0;
			if (p.hh < 0.5) p.hh = 0.0;
		}
		return n;
	}

	Quat ZTo(const Vec3& a_axis)
	{
		const Vec3   a = Normalize(a_axis, { 0, 0, 1 });
		const double d = std::clamp(a.z, -1.0, 1.0);
		if (d > 0.999999) return Quat{};
		if (d < -0.999999) return AxisAngle({ 1, 0, 0 }, kPi);
		return Normalize(AxisAngle(Normalize(Cross({ 0, 0, 1 }, a)), std::acos(d)));
	}

	PusherTarget TargetOf(const PusherSpec& a_p, std::span<const PartShape> a_shapes, double a_lead)
	{
		PusherTarget t;
		const PartShape* best = nullptr;
		double           bestB = -1;
		for (const auto& s : a_shapes) {
			if (s.part != a_p.part) continue;
			const double b = 0.5 * Length(s.b1 - s.a1) + s.r;
			if (b > bestB) {
				bestB = b;
				best = &s;
			}
		}
		if (!best) return t;
		t.c = Mid(best->a1, best->b1) + best->vel * std::max(0.0, a_lead);
		t.q = a_p.hh > 0 ? ZTo(best->b1 - best->a1) : Quat{};
		t.speed = Length(best->vel);
		t.v = best->vel;
		t.ok = IsFinite(t.c) && std::isfinite(t.speed) && IsFinite(t.v);
		return t;
	}

	// ------------------------------------------------------------------ path B, pushers.drive 1 (B2_lane2)
	DriveOut PusherFollow::Step(const DriveIn& a_in, const Cfg& a_c)
	{
		DriveOut o;
		const int n = std::clamp(a_in.n, 0, kPusherCap);
		// the true follow error: each body against where it was put last frame, its own motion over that frame allowed (a step that
		// integrated the velocity ends up to |v| x dt further on; one that did not leaves it where it was put)
		if (_have && a_in.solid && a_in.body && n == _n) {
			double err = 0;
			for (int i = 0; i < n; ++i) {
				const auto   k = static_cast<std::size_t>(i);
				const Vec3 step = _vel[k] * (std::clamp(std::max(_dt, a_in.dt), 0.0, 0.1) * 1.5);
				const double d = DistPointSeg(a_in.body[i], _put[k], _put[k] + step);
				if (std::isfinite(d)) err = std::max(err, std::max(0.0, d - 2.0));
				else err = 1e9;
			}
			o.followErr = err;
			errLast = err;
			errMax = std::max(errMax, err);
			++measured;
			// the self-test counts measured (solid) time only - a parked ghost is not driven and not judged. It is cleared only by a
			// measurement that is on target: a body that never takes a put (solid -> "jump" -> ghost -> put -> solid ...) keeps adding
			// up and is switched off, it never flickers solid / ghost forever (desktop PUSH-REPLAY-07, the "dead" world)
			_farFor = err > a_c.pFollowTolU ? _farFor + std::max(0.0, a_in.dt) : 0.0;
			o.notFollowing = _farFor > a_c.pFollowSec;
		} else if (!a_in.solid && !a_in.wantSolid) {
			_farFor = 0.0;  // parked because the plan wants them ghosts (not a jump): a fresh start next time they turn solid
		}
		// how far each body would be put this frame
		if (a_in.body && a_in.now)
			for (int i = 0; i < n; ++i)
				if (a_in.now[i].ok) o.snapMax = std::max(o.snapMax, Length(a_in.now[i].c - a_in.body[i]));
		// a part without a target this frame (no collider): never solid with a body we cannot put on its part
		bool allOk = a_in.now != nullptr && n > 0 && (!a_in.solid || a_in.body != nullptr);
		for (int i = 0; allOk && i < n; ++i) allOk = a_in.now[i].ok && IsFinite(a_in.now[i].c) && IsFinite(a_in.now[i].v) && (!a_in.body || IsFinite(a_in.body[i]));
		if (!a_in.wantSolid || !allOk) {
			o.act = a_in.solid ? DriveAct::kGoGhost : DriveAct::kPark;
			if (a_in.solid) ++ghostings;
			else ++parks;
			return o;
		}
		if (a_in.solid) {
			// a solid pusher is never teleported further than pushers.snapMax (a jump / blink of his body): ghost this frame
			if (o.snapMax > a_c.pSnapMaxU) {
				o.jump = true;
				o.act = DriveAct::kGoGhost;
				++jumps;
				++ghostings;
				return o;
			}
			o.act = DriveAct::kFollow;
			++follows;
			return o;
		}
		o.act = DriveAct::kSnapSolid;  // ghost -> solid: the put happens while still a ghost (no contact), any distance
		++snaps;
		return o;
	}
	void PusherFollow::Placed(const DriveIn& a_in, bool a_done)
	{
		const int n = std::clamp(a_in.n, 0, kPusherCap);
		if (!a_done || !a_in.now) {
			_have = false;
			return;
		}
		for (int i = 0; i < n; ++i) {
			const auto k = static_cast<std::size_t>(i);
			_put[k] = a_in.now[i].ok ? a_in.now[i].c : (a_in.body ? a_in.body[i] : Vec3{});
			_vel[k] = a_in.now[i].ok ? a_in.now[i].v : Vec3{};
		}
		_n = n;
		_dt = a_in.dt;
		_have = true;
	}

	PlanOut PusherPlan::Step(const PlanIn& a_in, const Cfg& a_c, bool a_exist, std::uintptr_t a_existWorld)
	{
		PlanOut o;
		if (a_in.propsNear) _lastProps = a_in.now;
		const bool recent = a_in.now - _lastProps <= a_c.pIdleKill && a_in.now >= _lastProps;
		const bool want = a_in.enabled && a_in.bodyReady && a_in.world != 0 && !a_in.loading && recent;
		if (a_exist) {
			if (!want || a_existWorld != a_in.world) {
				_solid = false;
				_slowSince = -1e9;
				++destroys;
				o.act = PlanAct::kDestroy;
				o.why = !a_in.enabled ? "off" : (a_in.loading ? "loading" : (a_existWorld != a_in.world ? "world" : (!a_in.bodyReady ? "body" : "idle")));
				return o;
			}
			const bool fast = a_in.maxSpeed > a_c.pMaxV;
			const bool slow = a_in.maxSpeed < 0.85 * a_c.pMaxV;
			const bool far = a_in.maxStepU > a_c.pMaxStepU;
			const bool propsNow = a_in.now - _lastProps <= 0.5 && a_in.now >= _lastProps;
			if (!slow || far || a_in.quiet || !propsNow) _slowSince = -1e9;
			else if (_slowSince < -1e8) _slowSince = a_in.now;
			bool solid = _solid;
			if (fast || far || a_in.quiet || !propsNow) solid = false;
			else if (_slowSince > -1e8 && a_in.now - _slowSince >= a_c.pSolidHold) solid = true;
			if (solid != _solid) {
				_solid = solid;
				solid ? ++solids : ++ghosts;
			}
			o.act = PlanAct::kDrive;
			o.solid = _solid;
			o.why = _solid ? "solid" : (fast ? "fast" : (far ? "far" : (a_in.quiet ? "quiet" : (!propsNow ? "noProps" : "hold"))));
			return o;
		}
		_solid = false;
		if (want && a_in.propsNear) {
			++creates;
			_slowSince = -1e9;
			o.act = PlanAct::kCreate;
			o.why = "props near";
		}
		return o;
	}

	// ------------------------------------------------------------------ the filter rows
	RowsVerdict CheckRows(const std::uint64_t* a_rows, std::uint64_t a_mask57)
	{
		(void)a_mask57;
		constexpr std::uint64_t b57 = 1ull << kPusherLayer;
		if ((a_rows[1] & ~b57) != kRow1 || (a_rows[4] & ~b57) != kRow4 || (a_rows[30] & ~b57) != kRow30 || (a_rows[32] & ~b57) != kRow32) return RowsVerdict::kChanged;
		// finB integrator (first game run 10-27 00:21: 'row57=FFFFFFFFFFFFFFFF' -> pushers off in every run): in the game an UNUSED layer's
		// row is all ones (the filter's default), not 0 as in the esm-built matrix (tests/s2_coll_matrix.json) - every esm layer's row
		// has bit 57 clear (rows 1 / 4 / 30 / 32 checked above). All ones = nobody's layer: free for us; ApplyRows then makes row 57
		// the loose props only. A row that is neither 0 nor all ones and holds other layers is someone else's layer (kTaken).
		constexpr std::uint64_t kUnusedRow = ~0ull;
		const std::uint64_t     r57 = a_rows[kPusherLayer];
		if (r57 != 0 && r57 != kUnusedRow && (r57 & ~(kLayProps | kLayCorpse)) != 0) return RowsVerdict::kTaken;
		// bit 57 on a layer we never use: someone else's layer (an all-ones row is an unused layer: no body lives on it)
		for (int j = 0; j < 64; ++j) {
			if (a_rows[j] == kUnusedRow) continue;
			if (((a_rows[j] >> kPusherLayer) & 1ull) && !(((kLayProps | kLayCorpse) >> j) & 1ull)) return RowsVerdict::kTaken;
		}
		return RowsVerdict::kOk;
	}
	void ApplyRows(std::uint64_t* a_rows, std::uint64_t a_mask57)
	{
		constexpr std::uint64_t b57 = 1ull << kPusherLayer;
		const std::uint64_t     m = a_mask57 & (kLayProps | kLayCorpse);
		a_rows[kPusherLayer] = m;
		for (int j = 0; j < 64; ++j) {
			if (!(((kLayProps | kLayCorpse) >> j) & 1ull)) continue;
			if ((m >> j) & 1ull) a_rows[j] |= b57;
			else a_rows[j] &= ~b57;
		}
	}
	std::uint64_t PusherMask(const Cfg& a_c) { return kLayProps | (a_c.pCorpses ? kLayCorpse : 0ull); }

	// ------------------------------------------------------------------ the load checks (bytes from the exe copy, re\bin\Fallout4.exe 1.11.240)
	std::span<const sp::r6check::Proof> Proofs()
	{
		static constexpr sp::r6check::Proof k[] = {
			// path A: the impulses (and the living foes' capsule push)
			{ "hknpBSWorld::applyBodyImpulseAt", 2721632, 0x18729A0, 0, "48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 E8 6C CF 9E FF 8B D3 48 8B", "shove.impulse" },
			{ "hknpWorld::applyBodyImpulseAt", 2255622, 0x125F920, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 50", "shove.impulse" },
			{ "hknpBSWorld::applyBodyLinearImpulse", 2721630, 0x1872940, 0, "48 89 5C 24 08 57 48 83 EC 20 8B DA 48 8B F9 E8 4C CD 9E FF 8B D3 48 8B", "shove.impulse" },
			// who is who: the collision group of an actor (his own filter info comes from look_hooks with the same function)
			{ "Actor::GetCollisionFilter", 2229991, 0xC81140, 0, "40 53 48 83 EC 20 48 8B 89 00 03 00 00 48 8B DA 48 85 C9 74 24 E8", "shove.groups" },
			// path B: the pushers
			{ "hknpWorld::createBody", 2255680, 0x12670A0, 0, "48 89 5C 24 18 44 89 4C 24 20 48 89 54 24 10 55 56 57 41 54 41 55 41 56", "shove.pushers" },
			{ "hknpWorld::createMotion", 2255688, 0x1269480, 0, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 30", "shove.pushers" },
			{ "hknpWorld::destroyBodies", 2255683, 0x1267BE0, 0, "44 89 4C 24 20 44 89 44 24 18 48 89 54 24 10 53 55 56 57 41 55 41 56 41", "shove.pushers" },
			{ "hknpWorld::destroyMotions", 2255689, 0x12695F0, 0, "40 53 55 57 41 54 41 55 48 83 EC 20 48 8B E9 49 63 D8 48 81 C1 90 06 00", "shove.pushers" },
			// the engine's own users prove the two unnamed ones: removeFromWorld -> destroyBodies, the physics system -> createMotion /
			// createBody / destroyMotions (rel32 inside one module: exact bytes)
			{ "hknpPhysicsSystem::removeFromWorld -> destroyBodies", 2255985, 0x127D1F0, 0xBD, "E8 2E A9 FE FF", "shove.pushers" },
			{ "hknpPhysicsSystem ctor -> createMotion", 2255982, 0x127C600, 0x304, "E8 77 CB FE FF", "shove.pushers" },
			{ "hknpPhysicsSystem ctor -> createBody", 2255982, 0x127C600, 0x4BC, "E8 DF A5 FE FF", "shove.pushers" },
			{ "hknpPhysicsSystem ctor -> destroyMotions", 2255982, 0x127C600, 0x519, "E8 D2 CA FE FF", "shove.pushers" },
			{ "hknpBodyCinfo::ctor", 2255967, 0x1279800, 0, "48 89 5C 24 08 57 48 83 EC 20 48 8B D9 48 83 C1 20 E8 1A 35 FE FF 33 FF", "shove.pushers" },
			{ "hknpMotionCinfo::ctor", 2258369, 0x13AB410, 0, "0F 57 C9 B8 FF FF 00 00 66 89 01 48 8B C1 C6 41 02 01 C7 41 04 00 00 80", "shove.pushers" },
			{ "hknpMotionCinfo::initializeAsKeyFramed", 2258373, 0x13ABF00, 0, "40 55 53 57 41 55 41 56 48 8D 6C 24 80 48 81 EC 80 01 00 00 4C 8D 72 40", "shove.pushers" },
			{ "hknpBSWorld::applyHardKeyFrame", 2277676, 0x18729D0, 0, "48 89 5C 24 08 57 48 81 EC 90 00 00 00 F3 0F 10 84 24 C0 00 00 00 48 8D", "shove.pushers" },
			{ "hknpWorld::setBodyCollisionFilterInfo", 2255626, 0x1260350, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20", "shove.pushers" },
			{ "hknpBSWorld::setBodyKeyframed", 2277684, 0x1872D40, 0, "48 89 74 24 10 57 48 83 EC 30 65 48 8B 04 25 58 00 00 00 8B FA 8B 15", "shove.pushers" },
			// B2_lane2 (pushers.drive 1, the engine's own keyframe recipe of bhkNPCollisionObject::ApplyHardKeyframe 2277943): put a body
			// (hkTransformf, activation 1; queued when TLS+0x1538 is set - the mov r11d, 0x1538 at +0x14) and set its velocities
			{ "hknpBSWorld::setBodyTransform", 2277668, 0x1872670, 0, "48 83 EC 78 44 8B 15 9D 09 60 02 65 48 8B 04 25 58 00 00 00 41 BB 38 15 00 00", "shove.pushers" },
			{ "hknpBSWorld::SetBodyVelocity", 2277670, 0x1872770, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 50 65 48 8B 04 25 58 00 00 00 8B DA", "shove.pushers" },
			// the pair rule reads the rows at bhkCollisionFilter +0x1A0 (mov rax, [r11 + r10 * 8 + 0x1A0], twice) and tests bit 14 first
			{ "pair test: bit 14 / layer & 0x7F", 2278061, 0x1887D50, 0, "44 8B D2 8B C2 41 83 E2 7F C1 E8 0E", "shove.pushers" },
			{ "pair test: rows at +0x1A0 (a)", 2278061, 0x1887D50, 0x92, "4B 8B 84 D3 A0 01 00 00", "shove.pushers" },
			{ "pair test: rows at +0x1A0 (b)", 2278061, 0x1887D50, 0xA5, "4B 8B 84 D3 A0 01 00 00", "shove.pushers" },
			{ "bhkCollisionFilter singleton", 4797438, 0x30F83C0, 0, "", "shove.pushers" },
			{ "exe tls index", 2787938, 0x3E73018, 0, "", "shove.pushers" },
			// B2 lane 3, the capsule pass: the filter write, and the pair rule it relies on (bytes from the exe copy, Ghidra 10-27 06:07):
			// a and b both group != 0 -> xor eax, r8d ; test eax, 0xffff0000 ; jz same-group (+0x47), and in the same-group branch a pair
			// that is not two biped / dead-biped / biped-no-cc layers does not collide: cmp r10d, 8 ; jz ; lea eax, [r10 - 0x20] ; cmp eax, 1 ;
			// ja no (+0x103). A prop given his group therefore never meets his capsule (layer 30), and still meets every other group.
			{ "hknpWorld::setBodyCollisionFilterInfo (capsule pass)", 2255626, 0x1260350, 0, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20", "shove.capsule" },
			{ "pair test: bit 14 / layer & 0x7F (capsule pass)", 2278061, 0x1887D50, 0, "44 8B D2 8B C2 41 83 E2 7F C1 E8 0E", "shove.capsule" },
			{ "pair test: same group -> own branch", 2278061, 0x1887D50, 0x47, "41 33 C0 A9 00 00 FF FF 74 54", "shove.capsule" },
			{ "pair test: same group, not biped layers -> no contact", 2278061, 0x1887D50, 0x103, "41 83 FA 08 74 09 41 8D 42 E0 83 F8 01 77 30", "shove.capsule" },
		};
		return { k, std::size(k) };
	}
}
