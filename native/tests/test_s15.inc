// R6 STEP 15 (CORE lane): "bodies shove bodies + his real physics body" - the pure units of core/shove.h (sp_tests runs them,
// s15_tests runs them too before its scenes). Every case builds its own Cfg (never the global tunables) except KY-15, which
// reads the shipped ini AS TEXT (no global state is changed).
//   CU-01 two-body contact: momentum kept (|dp| / p < 5 % - exact here), the mass ratio decides who moves, bounce 0
//   CU-02 the per-kind cap: dv = the cap, momentum still kept (the reaction is the same impulse)
//   CU-03 separating / resting contacts give nothing
//   CU-04 PushDir: head-on along his path, a graze sideways, a touch from the start
//   CL-01 the user rule in Classify: friend group / self / his target and flung bodies / static / keyframed / unknown living /
//         foes only with look.shoveFoes and the hit rules' yes / corpse / prop / the pushers' layers / a corpse at a friend's feet
//   AW-01 a push toward a friend is turned to awayDeg off the line to it; away pushes and far friends are untouched
//   FR-01 one frame on a sphere world: corpses / props pushed, friend bodies never (the assert counter 0), cooldown, reactions
//   PB-01 the pusher plan: made when props come near, ghost while fast, solid after the slow hold, ghost at once when fast or
//         quiet, destroyed on idle / world change / loading / key off
//   PB-02 the pushers' colliders and targets (capsule axis, lead)
//   RW-01 the layer-57 rows on the Fallout4.esm matrix: check / apply / idempotent / symmetric / never an actor layer
//   PR-01 the load-check table names every engine call the glue makes, and the unnamed ones by their engine callers
//   KY-15 the code defaults are the build before step 15; the shipped ini turns the paths on (read as text)
#include "core/shove.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace s15u
{
	namespace S = sp::shove;

	// a sphere world for the frame cases (FO4 u, kg): bodies are spheres; queries are exact sphere / capsule distances
	struct SBody
	{
		std::uint32_t id{ 0 }, layer{ 0 }, group{ 0 };
		Vec3          c{};
		double        r{ 10 }, m{ 10 };
		Vec3          v{};
		bool          keyframed{ false };
		Vec3          J{};       // impulses received (sum)
		int           pushes{ 0 };
		Vec3          comOverride{};  // finB FR-02: the motion's centre of mass the world reports (0 = not read, as before)
		std::uint32_t motion{ 0 }, shapeGeneration{ 0 };
		double        vmax{ 0 };      // B2 lane 3: the fastest it flew (u/s, after a world step)
		int           capHits{ 0 };   // B2 lane 3: contacts with the engine capsule model
		int           filterWrites{ 0 };
		double        floorZ{ 0.0 };  // B2 lane 3: the surface it rests on (u; 0 = the ground plane, as before)
	};
	// B2 lane 3: a model of his ENGINE capsule (the char rigid body the mover drives at his velocity): it meets the props of the layers
	// row 30 meets (S::kLayCapsuleJunk) unless they carry its group (the pair rule), as a velocity-driven body of unlimited mass with
	// restitution e (a light prop leaves at up to (1 + e) x its speed - finB's 6429 u/s prop after a ~4300 u/s dash)
	struct EngineCapsule
	{
		bool          on{ false };
		Vec3          pos{}, prev{};  // feet (u): where it ends this step / where it started
		double        r{ 20.7 }, h{ 147.0 }, e{ 0.5 };
		std::uint32_t group{ 7 };
	};
	struct SphereWorld final : S::IWorld
	{
		std::vector<SBody> b;
		int    begins{ 0 }, closest{ 0 }, nears{ 0 }, writes{ 0 };
		bool   busy{ false }, inSec{ false }, inWrite{ false };
		double us{ 0 };
		std::uint32_t selfGroup{ 0 };  // B2 lane 3: the engine's queries skip bodies of HIS group (0 = no skip, as before)
		bool allowRead{ true }, filterBusy{ false };
		std::function<void(SphereWorld&)> beforeWrite{};
		EngineCapsule cap{};           // B2 lane 3
		bool          comIsCentre{ false };  // B2 lane 3: Info reports each body's centre as its centre of mass (a removed body reads 0 in game)
		SBody* Find(std::uint32_t a_id)
		{
			for (auto& x : b)
				if (x.id == a_id) return &x;
			return nullptr;
		}
		bool   Begin() override { ++begins; inSec = allowRead; return allowRead; }
		void   End() override { inSec = false; }
		double Us() const override { return us; }
		bool   AnyNear(const Vec3& a_lo, const Vec3& a_hi, std::uint64_t a_mask) override
		{
			++nears;
			us += 2;
			for (const auto& x : b) {
				if (!((a_mask >> x.layer) & 1ull)) continue;
				if (x.c.x + x.r < a_lo.x || x.c.x - x.r > a_hi.x || x.c.y + x.r < a_lo.y || x.c.y - x.r > a_hi.y || x.c.z + x.r < a_lo.z || x.c.z - x.r > a_hi.z) continue;
				return true;
			}
			return false;
		}
		int Closest(const Vec3& a_a, const Vec3& a_b, double a_r, double a_maxDist, std::uint64_t a_mask, S::BodyHit* a_out, int a_cap) override
		{
			++closest;
			us += 8;
			int        k = 0;
			const Vec3 d = a_b - a_a;
			const double L2 = LengthSq(d);
			for (const auto& x : b) {
				if (!((a_mask >> x.layer) & 1ull) || k >= a_cap) continue;
				if (selfGroup != 0 && x.group == selfGroup) continue;  // B2 lane 3: his own group is invisible to his queries
				const double t = L2 > 1e-12 ? std::clamp(Dot(x.c - a_a, d) / L2, 0.0, 1.0) : 0.0;
				const Vec3   q = a_a + d * t;
				const double dc = Length(q - x.c);
				const double sep = dc - a_r - x.r;
				if (sep > a_maxDist) continue;
				const Vec3 n = Normalize(q - x.c, { 0, 0, 1 });
				a_out[k++] = { x.id, x.layer, x.group, x.c + n * x.r, n, sep };
			}
			return k;
		}
		S::BodyInfo Info(std::uint32_t a_id) override
		{
			S::BodyInfo o;
			if (const SBody* x = Find(a_id)) {
				o.ok = true;
				o.dynamic = !x->keyframed;
				o.keyframed = x->keyframed;
				o.invMass = x->keyframed ? 0.0 : 1.0 / x->m;
				o.vel = x->v;
				o.motion = x->motion;
				o.com = x->comOverride;  // finB FR-02: a compound object's bodies share one motion (one centre of mass); 0 = not read
				if (comIsCentre && LengthSq(x->comOverride) == 0) o.com = x->c;
			}
			return o;
		}
		bool BeginWrite() override
		{
			if (busy) return false;
			if(beforeWrite)beforeWrite(*this);
			inWrite = true;
			return true;
		}
		void EndWrite() override { inWrite = false; }
		bool Impulse(std::uint32_t a_id, const Vec3& a_J, const Vec3&) override
		{
			SBody* x = Find(a_id);
			if (!x || !inWrite) return false;
			x->v += a_J / x->m;
			x->J += a_J;
			++x->pushes;
			++writes;
			return true;
		}
		bool LinearImpulse(std::uint32_t a_id, const Vec3& a_J) override { return Impulse(a_id, a_J, {}); }
		// B2 lane 3: filter info = (group << 16) | layer, the shape = a fake pointer per body
		bool Filter(std::uint32_t a_id, std::uint32_t& a_info, std::uintptr_t& a_shape) override
		{
			const SBody* x = Find(a_id);
			if (!x || (!inSec && !inWrite) || filterBusy) return false;
			a_info = (x->group << 16) | (x->layer & 0x7Fu);
			a_shape = 0x10000u + x->id + static_cast<std::uintptr_t>(x->shapeGeneration) * 0x100000u;
			return true;
		}
		bool BodyGone(std::uint32_t a_id) override { return inWrite && Find(a_id)==nullptr; }
		bool SetFilter(std::uint32_t a_id, std::uint32_t a_info) override
		{
			SBody* x = Find(a_id);
			if (!x || !inWrite) return false;
			x->group = a_info >> 16;
			x->layer = a_info & 0x7Fu;
			++x->filterWrites;
			return true;
		}
		// a simple world step: free flight + a ground at z = r with heavy friction (bodies settle). B2 lane 3: plus the engine capsule
		// model, swept from cap.prev to cap.pos in steps of <= 8 u
		void Step(double a_dt)
		{
			for (auto& x : b) {
				if (x.keyframed) continue;
				x.v.z -= 980.0 * a_dt;
				x.c += x.v * a_dt;
				if (x.c.z < x.floorZ + x.r) {
					x.c.z = x.floorZ + x.r;
					if (x.v.z < 0) x.v.z = 0;
					x.v.x *= std::max(0.0, 1.0 - 6.0 * a_dt);
					x.v.y *= std::max(0.0, 1.0 - 6.0 * a_dt);
				}
			}
			if (cap.on && a_dt > 0) {
				const Vec3   vc = (cap.pos - cap.prev) / a_dt;
				const int    n = std::clamp(static_cast<int>(std::ceil(Length(cap.pos - cap.prev) / 8.0)), 1, 400);
				for (int k = 1; k <= n; ++k) {
					const Vec3 p = cap.prev + (cap.pos - cap.prev) * (static_cast<double>(k) / n);
					const Vec3 a = p + Vec3{ 0, 0, cap.r }, bb = p + Vec3{ 0, 0, cap.h - cap.r };
					for (auto& x : b) {
						if (x.keyframed || !((S::kLayCapsuleJunk >> x.layer) & 1ull) || (x.group != 0 && x.group == cap.group)) continue;
						const double t = std::clamp((x.c.z - a.z) / std::max(1e-9, bb.z - a.z), 0.0, 1.0);
						const Vec3   q = a + (bb - a) * t;
						const Vec3   d = x.c - q;
						const double dist = Length(d);
						if (dist >= cap.r + x.r) continue;
						const Vec3   nrm = Normalize(Vec3{ d.x, d.y, dist > 1e-6 ? d.z : 0.0 }, Normalize(Vec3{ vc.x, vc.y, 0 }, { 1, 0, 0 }));
						const double vrel = Dot(x.v - vc, nrm);
						if (vrel < 0) x.v += nrm * (-(1.0 + cap.e) * vrel);
						x.c = q + nrm * (cap.r + x.r);  // out of the capsule
						++x.capHits;
					}
				}
				cap.prev = cap.pos;
			}
			for (auto& x : b) x.vmax = std::max(x.vmax, Length(x.v));
		}
	};

	// one of his parts as a moving sphere (FO4 u)
	inline S::PartShape Sph(int a_part, const Vec3& a_c0, const Vec3& a_c1, double a_r, const Vec3& a_v)
	{
		S::PartShape p;
		p.part = a_part;
		p.kind = 0;
		p.a0 = p.b0 = a_c0;
		p.a1 = p.b1 = a_c1;
		p.r = a_r;
		p.vel = a_v;
		return p;
	}
	inline std::array<double, sp::body::kPartCount> Masses()
	{
		std::array<double, sp::body::kPartCount> m{};
		for (auto& x : m) x = 30.0;
		m[sp::body::kHip] = m[sp::body::kTorso] = 160.0;
		return m;
	}
	inline S::Cfg OnCfg()
	{
		S::Cfg c;
		c.on = true;
		c.reactMaxDv = 1e9;
		return c;
	}

	// B2 lane 3: the capsule pass / governor / minPartV / victimR units (included INSIDE this namespace; no headers of its own)
#include "test_s15_b2l3.inc"
#include "test_s15_finish.inc"

	inline void Run()
	{
		using sp::shove::kToGame;
		// ---- CU-01 momentum / mass ratio / bounce 0
		{
			const Vec3   dir{ 1, 0, 0 };
			const Vec3   vH{ 4000, 300, 0 }, vB{ 100, 0, 0 };
			const double mH = 160, mB = 12;
			const auto   imp = S::Contact(dir, vH, mH, vB, mB, 0.0, 1e12);
			CHECK(imp.ok, "CU-01 a contact");
			const Vec3 vH2 = vH - imp.J / mH, vB2 = vB + imp.J / mB;
			const Vec3 p0 = vH * mH + vB * mB, p1 = vH2 * mH + vB2 * mB;
			CHECK(Length(p1 - p0) <= 0.05 * Length(p0) && Length(p1 - p0) < 1e-6 * Length(p0), "CU-01 momentum kept (dp %.3g of %.3g)", Length(p1 - p0), Length(p0));
			CHECK(std::abs(Dot(vH2 - vB2, dir)) < 1e-6, "CU-01 bounce 0: no relative normal speed after (%.3g)", Dot(vH2 - vB2, dir));
			CHECK(std::abs(Dot(vH2 - vH, { 0, 1, 0 })) < 1e-9, "CU-01 no friction impulse (the tangent speed kept)");
			const auto light = S::Contact(dir, vH, mH, vB, 2.0, 0.0, 1e12);
			const auto heavy = S::Contact(dir, vH, mH, vB, 400.0, 0.0, 1e12);
			CHECK(light.dv > imp.dv && imp.dv > heavy.dv, "CU-01 mass ratio: light %.0f > corpse %.0f > heavy %.0f u/s", light.dv, imp.dv, heavy.dv);
			CHECK(std::abs(heavy.dv - 3900.0 * 160.0 / 560.0) < 1e-6, "CU-01 dv = vn x mHis / (mHis + mBody)");
		}
		// ---- CU-02 the cap
		{
			const auto imp = S::Contact({ 1, 0, 0 }, { 4000, 0, 0 }, 160, {}, 10, 0.0, 7.0 * kToGame);
			CHECK(imp.ok && imp.capped && std::abs(imp.dv - 7.0 * kToGame) < 1e-9, "CU-02 capped at 7 Havok m/s (%.1f u/s)", imp.dv);
			const Vec3 dvH = -imp.J / 160.0, dvB = imp.J / 10.0;
			CHECK(Length(dvH * 160.0 + dvB * 10.0) < 1e-9, "CU-02 the reaction equals the capped impulse (momentum kept)");
		}
		// ---- CU-03 separating / resting
		{
			CHECK(!S::Contact({ 1, 0, 0 }, { -100, 0, 0 }, 160, {}, 10, 0.0, 1e9).ok, "CU-03 moving away: no impulse");
			CHECK(!S::Contact({ 1, 0, 0 }, { 20, 0, 0 }, 160, {}, 10, 0.4 * kToGame, 1e9).ok, "CU-03 resting (below minVn): no impulse");
			CHECK(!S::Contact({ 1, 0, 0 }, { 4000, 0, 0 }, 160, {}, 0, 0, 1e9).ok, "CU-03 a static body (no mass): no impulse");
		}
		// ---- CU-04 PushDir
		{
			const Vec3 c0{ 0, 0, 100 }, c1{ 200, 0, 100 };
			const Vec3 head = S::PushDir(c0, c1, 20, { 150, 0, 100 }, { -1, 0, 0 });
			CHECK(head.x > 0.99, "CU-04 head-on: along his path (%.2f %.2f %.2f)", head.x, head.y, head.z);
			const Vec3 graze = S::PushDir(c0, c1, 20, { 100, 18, 100 }, { 0, -1, 0 });
			CHECK(graze.y > 0.3 && graze.x > 0.3, "CU-04 a graze: forward and sideways (%.2f %.2f)", graze.x, graze.y);
			const Vec3 side = S::PushDir(c0, c1, 20, { 100, 30, 100 }, { 0, -1, 0 });
			CHECK(side.y > 0.9, "CU-04 a body beside the path: sideways (%.2f)", side.y);
			const Vec3 touch = S::PushDir(c0, c0, 20, { 15, 0, 100 }, { -1, 0, 0 });
			CHECK(touch.x > 0.99, "CU-04 touching, not moving: out from his centre");
		}
		// ---- CL-01 the user rule
		{
			S::Cfg c = OnCfg();
			S::FrameIn in;
			in.selfGroup = 7;
			std::vector<S::Friend> fr{ { 1, 100, { 0, 0, 100 }, false, true }, { 2, 101, { 5000, 0, 100 }, true, false } };
			std::vector<S::Foe>    fo{ { 3, 200, true }, { 4, 201, false } };
			std::vector<std::uint32_t> skip{ 300 };
			in.friends = fr;
			in.foes = fo;
			in.skipGroups = skip;
			S::BodyInfo dyn{ true, true, false, 0.1, {} };
			S::BodyInfo kf{ true, true, true, 0.0, {} };
			auto who = [&](std::uint32_t layer, std::uint32_t group, const S::BodyInfo& i, Vec3 p = { 2000, 2000, 0 }) {
				S::BodyHit h;
				h.layer = layer;
				h.group = group;
				h.p = p;
				return S::Classify(h, i, in, c, nullptr);
			};
			CHECK(who(32, 100, dyn) == S::Who::kFriend, "CL-01 the player's group (even a ragdoll of it) is a friend");
			CHECK(who(32, 101, dyn) == S::Who::kFriend, "CL-01 a downed companion's ragdoll (corpse layer, friend group) is a friend");
			CHECK(who(30, 100, dyn) == S::Who::kFriend, "CL-01 the player's capsule is a friend");
			CHECK(who(4, 7, dyn) == S::Who::kSelf, "CL-01 his own group");
			CHECK(who(32, 300, dyn) == S::Who::kSkip, "CL-01 his target / a flung body is the fling's");
			CHECK(who(32, 500, kf) == S::Who::kStatic, "CL-01 a keyframed body is never pushed");
			CHECK(who(4, 500, S::BodyInfo{ true, false, false, 0.0, {} }) == S::Who::kStatic, "CL-01 a static prop is never pushed");
			CHECK(who(30, 999, dyn) == S::Who::kLiving, "CL-01 an unknown living capsule is never pushed");
			CHECK(who(30, 200, dyn) == S::Who::kLiving, "CL-01 foes off (look.shoveFoes 0): a foe is not pushed");
			c.foes = true;
			CHECK(who(30, 200, dyn) == S::Who::kFoe, "CL-01 foes on: an allowed foe's capsule");
			CHECK(who(30, 201, dyn) == S::Who::kLiving, "CL-01 foes on: a foe the hit rules refuse is not pushed");
			CHECK(who(8, 200, dyn) == S::Who::kLiving, "CL-01 foes on: only the capsule, never the hit bodies (layer 8)");
			CHECK(who(32, 500, dyn) == S::Who::kCorpse, "CL-01 a corpse");
			CHECK(who(4, 500, dyn) == S::Who::kProp && who(55, 501, dyn) == S::Who::kProp && who(29, 502, dyn) == S::Who::kProp, "CL-01 loose props 4 / 55 / 29");
			CHECK(who(1, 500, dyn) == S::Who::kNone && who(17, 500, dyn) == S::Who::kNone, "CL-01 statics / terrain: none");
			CHECK(who(32, 500, dyn, { 60, 0, 10 }) == S::Who::kNearFriend, "CL-01 a corpse at the player's feet is left alone");
			CHECK(who(4, 500, dyn, { 0, 300, 10 }) == S::Who::kProp, "CL-01 ... 300 u away it is pushed");
			in.pushersSolid = true;
			in.pusherLayers = S::kLayProps;
			CHECK(who(4, 500, dyn) == S::Who::kPusher && who(32, 500, dyn) == S::Who::kCorpse, "CL-01 solid pushers own the props, corpses stay with the impulses");
			c.corpses = false;
			CHECK(who(32, 500, dyn) == S::Who::kNone, "CL-01 shove.corpses 0");
			CHECK(!S::Pushable(S::Who::kFriend) && !S::Pushable(S::Who::kNearFriend) && !S::Pushable(S::Who::kLiving) && S::Pushable(S::Who::kCorpse), "CL-01 Pushable");
		}
		// ---- AW-01 never toward a friend
		{
			std::vector<S::Friend> fr{ { 1, 100, { 1000, 0, 100 }, false, true } };
			bool       t = false;
			const Vec3 d = S::AwayFromFriends(Normalize(Vec3{ 1, 0.1, 0.2 }), { 0, 0, 0 }, fr, 60.0, 1500.0, &t);
			const double ang = AngleDeg(Vec3{ d.x, d.y, 0 }, Vec3{ 1000, 0, 0 });
			CHECK(t && ang >= 59.9 && ang <= 60.1, "AW-01 turned to 60 deg off the player (%.2f)", ang);
			CHECK(std::abs(d.z - Normalize(Vec3{ 1, 0.1, 0.2 }).z) < 0.05 && d.y > 0, "AW-01 the side and the climb kept");
			const Vec3 away = S::AwayFromFriends({ -1, 0, 0 }, { 0, 0, 0 }, fr, 60.0, 1500.0, &t);
			CHECK(!t && away.x < -0.99, "AW-01 a push away from the player is untouched");
			std::vector<S::Friend> farF{ { 1, 100, { 5000, 0, 100 }, false, true } };
			S::AwayFromFriends({ 1, 0, 0 }, { 0, 0, 0 }, farF, 60.0, 1500.0, &t);
			CHECK(!t, "AW-01 a friend beyond awayR does not turn it");
		}
		// ---- FR-01 one frame on a sphere world
		{
			SphereWorld w;
			w.b.push_back({ 11, 32, 500, { 300, 0, 100 }, 15, 10 });   // a corpse bone on his path
			w.b.push_back({ 12, 4, 501, { 320, 22, 95 }, 6, 1.5 });    // a can next to it
			w.b.push_back({ 13, 32, 101, { 250, -10, 100 }, 15, 10 }); // the downed companion's ragdoll (friend group) on the path
			w.b.push_back({ 14, 30, 100, { 360, 0, 100 }, 30, 90 });   // the player's capsule (friend; not in the mask anyway)
			w.b.push_back({ 15, 4, 502, { 330, -30, 100 }, 6, 1.5, {}, true });  // a keyframed prop
			std::vector<S::Friend> fr{ { 1, 100, { 2500, 2500, 100 }, false, true }, { 2, 101, { -3000, 0, 100 }, true, false } };
			auto  m = Masses();
			std::vector<S::PartShape> sh{ Sph(sp::body::kTorso, { 0, 0, 100 }, { 400, 0, 100 }, 20, { 24000, 0, 0 }), Sph(sp::body::kHip, { -10, 0, 80 }, { 390, 0, 80 }, 20, { 24000, 0, 0 }) };
			S::FrameIn in;
			in.now = 10.0;
			in.dt = 1.0 / 60.0;
			in.selfGroup = 7;
			in.shapes = sh;
			in.partMass = m.data();
			in.totalMass = 1000;
			in.friends = fr;
			S::Shover sv;
			const S::Cfg c = OnCfg();
			const auto   o = sv.Frame(in, c, w, false);
			CHECK(o.propsNear && o.pushes == 2, "FR-01 the corpse bone and the can pushed (pushes %d)", o.pushes);
			CHECK(w.Find(11)->pushes == 1 && w.Find(11)->v.x > 0, "FR-01 the corpse moves forward (%.0f u/s)", w.Find(11)->v.x);
			CHECK(w.Find(11)->v.x <= c.maxDvCorpse * kToGame + 1e-6 && w.Find(12)->v.x <= c.maxDvProp * kToGame + 1e-6, "FR-01 caps (corpse %.0f, can %.0f u/s)", w.Find(11)->v.x, Length(w.Find(12)->v));
			CHECK(w.Find(13)->pushes == 0 && w.Find(14)->pushes == 0 && w.Find(15)->pushes == 0, "FR-01 the companion's ragdoll, the player and the keyframed prop never");
			CHECK(sv.St().friendImpulses == 0 && sv.St().friendSkips >= 1, "FR-01 the assert counter stays 0 (friend skips %llu)", static_cast<unsigned long long>(sv.St().friendSkips));
			Vec3 Jtot{}, Rtot{};
			for (const auto& x : w.b) Jtot += x.J;
			for (int i = 0; i < o.nReact; ++i) Rtot += o.react[static_cast<std::size_t>(i)].J;
			CHECK(Length(Jtot + Rtot) < 1e-6 * std::max(1.0, Length(Jtot)), "FR-01 the reactions are the equal and opposite impulses (momentum kept)");
			const auto o2 = sv.Frame(in, c, w, false);
			CHECK(o2.pushes == 0, "FR-01 the cooldown: the same contact is not pushed again in the next frame");
			w.busy = true;
			in.now = 11.0;
			for (auto& x : w.b) x.v = {};  // finB: they came to rest (a body still flying away at its cap is not pushed again - FR-02)
			const auto o3 = sv.Frame(in, c, w, false);
			CHECK(o3.pushes == 0 && sv.St().writeBusy == 1, "FR-01 a busy world lock writes nothing (never waits)");
			S::Cfg off;
			const auto o4 = sv.Frame(in, off, w, false);
			CHECK(o4.queries == 0 && !o4.propsNear, "FR-01 keys off: no query at all (the build before step 15)");
		}
		// ---- FR-02 (finB integrator; the first game run: one multi-body object = one motion, each body got the full push in the same
		// frame -> 4181 u/s): bodies that share a motion (the same centre of mass) get ONE push a frame; a body already moving away at its
		// cap gets none
		{
			SphereWorld w;
			const Vec3  shared{ 330, 0, 100 };
			SBody       p1{ 21, 4, 601, { 320, -8, 100 }, 6, 2 };
			SBody       p2{ 22, 4, 602, { 330, 8, 100 }, 6, 2 };
			SBody       p3{ 23, 4, 603, { 340, 0, 100 }, 6, 2 };
			p1.comOverride = p2.comOverride = p3.comOverride = shared;
			w.b = { p1, p2, p3 };
			std::vector<S::Friend> fr{ { 1, 100, { 2500, 2500, 100 }, false, true } };
			auto m = Masses();
			std::vector<S::PartShape> sh{ Sph(sp::body::kTorso, { 0, 0, 100 }, { 400, 0, 100 }, 20, { 24000, 0, 0 }) };
			S::FrameIn in;
			in.now = 20.0;
			in.dt = 1.0 / 60.0;
			in.selfGroup = 7;
			in.shapes = sh;
			in.partMass = m.data();
			in.totalMass = 1000;
			in.friends = fr;
			S::Shover    sv;
			const S::Cfg c = OnCfg();
			const auto   o = sv.Frame(in, c, w, false);
			CHECK(o.pushes == 1, "FR-02 three bodies of one motion: one push a frame (pushes %d)", o.pushes);
			SphereWorld w2;
			SBody       fast{ 31, 4, 701, { 320, 0, 100 }, 6, 2 };
			fast.v = { c.maxDvProp * kToGame + 50.0, 0, 0 };  // already flying away along his path faster than the cap
			w2.b = { fast };
			S::Shover sv2;
			const auto o2 = sv2.Frame(in, c, w2, false);
			CHECK(o2.pushes == 0, "FR-02 a body already moving away at its cap gets no push (no pumping past the cap)");
		}
		// ---- B2 codex review regressions (cx3 deliver\shove_regressions.inc, adapted: the fixes sit behind shove.capFinal /
		// shove.friendRecheck, code default 0 = the old rule, so every case runs the key on AND shows the old behaviour with it off)
		// FR-03 (SHV-04): one contact never takes a body past its cap speed (vector speed), even from just under it or sideways;
		// a body already past it may be braked, never sped up; the per-contact delta cap stays
		{
			auto c = OnCfg();
			c.capFinal = true;
			auto m = Masses();
			std::vector<S::PartShape> sh{ Sph(sp::body::kTorso, { 0, 0, 100 }, { 400, 0, 100 }, 20, { 24000, 0, 0 }) };
			S::FrameIn in;
			in.now = 20.0;
			in.dt = 1.0 / 60.0;
			in.selfGroup = 7;
			in.shapes = sh;
			in.partMass = m.data();
			in.totalMass = 1000;
			const double cap = c.maxDvProp * kToGame;
			const Vec3   vs[] = { { cap - 1.0, 0, 0 }, { 0, 0.99 * cap, 0 }, { -2.0 * cap, 0, 0 }, { -cap, 2.0 * cap, 0 }, { cap, cap, 0 } };
			for (int k = 0; k < 5; ++k) {
				SphereWorld w;
				w.b.push_back({ 31, 4, 701, { 320, 0, 100 }, 6, 2, vs[k] });
				S::Shover    sv;
				const double before = Length(vs[k]);
				const auto   o = sv.Frame(in, c, w, false);
				const double after = Length(w.Find(31)->v);
				CHECK(after <= std::max(before, cap) + 1e-6, "FR-03 case %d speed %.1f -> %.1f (cap %.1f)", k, before, after, cap);
				CHECK(Length(w.Find(31)->J) / 2.0 <= cap + 1e-6, "FR-03 case %d per-contact delta cap kept", k);
				if (k < 2) CHECK(o.pushes == 1 && after <= cap + 1e-6, "FR-03 case %d a below-cap contact is pushed and stays within the cap (pushes %d, %.1f)", k, o.pushes, after);
				if (k == 4) CHECK(o.pushes == 0, "FR-03 already past the cap moving away: no impulse");
			}
			// the old rule (shove.capFinal 0): cap - 1 u/s along the push takes another full cap (~2x) - the defect the key fixes
			auto old = OnCfg();
			SphereWorld w;
			w.b.push_back({ 31, 4, 701, { 320, 0, 100 }, 6, 2, vs[0] });
			S::Shover sv;
			(void)sv.Frame(in, old, w, false);
			CHECK(Length(w.Find(31)->v) > 1.5 * cap, "FR-03 capFinal 0 = the old rule (cap - 1 -> %.0f u/s, cap %.0f)", Length(w.Find(31)->v), cap);
		}
		// AW-02 (SHV-03): protecting a later friend cannot undo the protection of the player (player 0 deg, a friend at 100 deg)
		{
			const double           b = 100.0 * kDeg2Rad;
			std::vector<S::Friend> fr{ { 1, 100, { 500, 0, 0 }, false, true }, { 2, 101, { 500 * std::cos(b), 500 * std::sin(b), 0 }, false, false } };
			bool                   turned = false;
			const Vec3             d = S::AwayFromFriends({ 1, 0, 0 }, {}, fr, 60.0, 1500.0, &turned, true);
			const bool             refused = LengthSq(d) < 1e-12;
			CHECK(refused || (AngleDeg(d, fr[0].pos) >= 60.0 - 1e-6 && AngleDeg(d, fr[1].pos) >= 60.0 - 1e-6), "AW-02 every friend clear after the final turn (refused %d)", refused);
			if (refused) CHECK(!S::Contact(d, { 4000, 0, 0 }, 160, {}, 2, 0, 14 * kToGame).ok, "AW-02 the zero direction really refuses the impulse");
			const Vec3 dOld = S::AwayFromFriends({ 1, 0, 0 }, {}, fr, 60.0, 1500.0, &turned);
			CHECK(AngleDeg(dOld, fr[0].pos) < 59.0, "AW-02 friendRecheck 0 = the old turns (%.1f deg off the player)", AngleDeg(dOld, fr[0].pos));
			// one friend: the recheck changes nothing (AW-01's 60 deg turn)
			std::vector<S::Friend> one{ fr[0] };
			const Vec3             d1 = S::AwayFromFriends({ 1, 0, 0 }, {}, one, 60.0, 1500.0, &turned, true);
			CHECK(turned && std::abs(AngleDeg(d1, one[0].pos) - 60.0) < 0.1, "AW-02 a single friend: still the 60 deg turn (%.2f)", AngleDeg(d1, one[0].pos));
		}
		// FR-04 (SHV-05): the motion id is the identity - one shared motion centred at the world origin gets one push; two distinct
		// motions whose centres coincide get one each
		{
			auto c = OnCfg();
			auto m = Masses();
			std::vector<S::PartShape> sh{ Sph(sp::body::kTorso, { 0, 0, 100 }, { 400, 0, 100 }, 20, { 24000, 0, 0 }) };
			S::FrameIn in;
			in.now = 20.0;
			in.dt = 1.0 / 60.0;
			in.selfGroup = 7;
			in.shapes = sh;
			in.partMass = m.data();
			in.totalMass = 1000;
			for (int k = 0; k < 2; ++k) {
				SphereWorld w;
				SBody       a{ 41, 4, 601, { 320, -8, 100 }, 6, 2 };
				SBody       bb{ 42, 4, 602, { 330, 8, 100 }, 6, 2 };
				if (k == 0) {
					a.motion = bb.motion = 901;
					a.comOverride = bb.comOverride = {};  // a valid shared motion centred at the origin
				} else {
					a.motion = 902;
					bb.motion = 903;
					a.comOverride = bb.comOverride = { 330, 0, 100 };  // distinct overlapping motions
				}
				w.b = { a, bb };
				S::Shover  sv;
				const auto o = sv.Frame(in, c, w, false);
				CHECK(o.pushes == (k == 0 ? 1 : 2), "FR-04 case %d: %d pushes expected, got %d", k, k == 0 ? 1 : 2, o.pushes);
			}
		}
		// ---- PB-01 the pusher plan
		{
			S::Cfg c;
			c.pushers = true;
			S::PusherPlan pl;
			S::PlanIn in;
			in.enabled = true;
			in.bodyReady = true;
			in.world = 0x1000;
			in.now = 1.0;
			auto act = pl.Step(in, c, false, 0);
			CHECK(act.act == S::PlanAct::kNone, "PB-01 nothing without props near");
			in.propsNear = true;
			act = pl.Step(in, c, false, 0);
			CHECK(act.act == S::PlanAct::kCreate && !act.solid, "PB-01 props near: made (as ghosts)");
			in.maxSpeed = 3000;
			in.now = 1.1;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.act == S::PlanAct::kDrive && !act.solid, "PB-01 fast: ghost");
			in.maxSpeed = 300;
			in.now = 1.2;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(!act.solid, "PB-01 slow but not held yet: ghost");
			in.now = 1.5;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.solid, "PB-01 slow for the hold: solid");
			in.maxSpeed = 1500;
			in.now = 1.52;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(!act.solid, "PB-01 fast again: ghost at once");
			in.maxSpeed = 300;
			in.now = 1.6;
			pl.Step(in, c, true, 0x1000);
			in.now = 1.9;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.solid, "PB-01 solid again after the hold");
			in.quiet = true;
			in.now = 1.95;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(!act.solid, "PB-01 quiet (a downed friend near, pushers.corpses 1): ghost");
			in.quiet = false;
			in.maxStepU = 900;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(!act.solid, "PB-01 a pusher far from its target (a jump): ghost");
			in.maxStepU = 0;
			in.propsNear = false;
			in.now = 1.95 + 0.6;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.act == S::PlanAct::kDrive && !act.solid, "PB-01 no props for 0.6 s: ghost, kept");
			in.now = 1.9 + c.pIdleKill + 0.2;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.act == S::PlanAct::kDestroy, "PB-01 no props for pushers.idleKill: destroyed (%s)", act.why);
			in.propsNear = true;
			in.now = 10.0;
			pl.Step(in, c, false, 0);
			act = pl.Step(in, c, true, 0x2000);
			CHECK(act.act == S::PlanAct::kDestroy && std::string(act.why) == "world", "PB-01 another world: destroyed");
			in.loading = true;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.act == S::PlanAct::kDestroy && std::string(act.why) == "loading", "PB-01 a load / teleport coming: destroyed");
			in.loading = false;
			in.enabled = false;
			act = pl.Step(in, c, true, 0x1000);
			CHECK(act.act == S::PlanAct::kDestroy && std::string(act.why) == "off", "PB-01 the key off: destroyed");
		}
		// ---- PB-02 colliders and targets
		{
			std::vector<S::PartShape> sh;
			S::PartShape thigh;
			thigh.part = sp::body::kLegL;
			thigh.kind = 1;
			thigh.a1 = { 0, 0, 60 };
			thigh.b1 = { 0, 30, 30 };
			thigh.r = 5;
			thigh.vel = { 600, 0, 0 };
			sh.push_back(thigh);
			sh.push_back(Sph(sp::body::kHip, { 0, 0, 80 }, { 0, 0, 80 }, 12, { 600, 0, 0 }));
			std::array<S::PusherSpec, 13> sp{};
			const int n = S::ChoosePushers(sh, 9, sp.data(), 13);
			CHECK(n == 2 && sp[0].part == sp::body::kHip && sp[1].part == sp::body::kLegL, "PB-02 the order (hip first, then the legs)");
			CHECK(sp[1].hh > 20 && sp[1].hh < 22 && sp[1].r == 5 && sp[0].hh == 0, "PB-02 a thigh capsule (half length %.1f), the hip a sphere", sp[1].hh);
			const auto t = S::TargetOf(sp[1], sh, 0.5);
			CHECK(t.ok && std::abs(t.c.x - 300) < 1e-9 && std::abs(t.c.y - 15) < 1e-9, "PB-02 the target leads by vel x lead");
			const Vec3 axis = t.q.Rotate({ 0, 0, 1 });
			CHECK(Length(axis - Normalize(thigh.b1 - thigh.a1)) < 1e-9, "PB-02 local +z on the thigh axis");
			CHECK(Length(S::ZTo({ 0, 0, -1 }).Rotate({ 0, 0, 1 }) - Vec3{ 0, 0, -1 }) < 1e-9 && Length(S::ZTo({ 0, 0, 1 }).Rotate({ 0, 0, 1 }) - Vec3{ 0, 0, 1 }) < 1e-12, "PB-02 ZTo poles");
		}
		// ---- RW-01 the rows on the esm matrix
		{
			std::uint64_t rows[64]{};
			bool          have = false;
			for (const char* p : { "tests/s2_coll_matrix.json", "../tests/s2_coll_matrix.json", "s2_coll_matrix.json" }) {
				std::ifstream f(p);
				if (!f) continue;
				std::stringstream ss;
				ss << f.rdbuf();
				const std::string js = ss.str();
				// "idx": N ... "collidesIdx": [ ... ]
				std::size_t at = 0;
				while ((at = js.find("\"idx\":", at)) != std::string::npos) {
					const int   idx = std::atoi(js.c_str() + at + 6);
					const auto  ci = js.find("\"collidesIdx\"", at);
					const auto  lb = js.find('[', ci), rb = js.find(']', ci);
					if (ci == std::string::npos || lb == std::string::npos || rb == std::string::npos || idx < 0 || idx >= 64) break;
					std::stringstream ls(js.substr(lb + 1, rb - lb - 1));
					std::string       tok;
					while (std::getline(ls, tok, ',')) {
						const int j = std::atoi(tok.c_str());
						if (j >= 0 && j < 64) rows[idx] |= 1ull << j;
					}
					at = rb;
				}
				have = true;
				break;
			}
			CHECK(have, "RW-01 tests/s2_coll_matrix.json read");
			if (have) {
				CHECK(rows[1] == S::kRow1 && rows[4] == S::kRow4 && rows[30] == S::kRow30 && rows[32] == S::kRow32, "RW-01 the esm rows the check expects");
				CHECK(S::CheckRows(rows, S::kLayProps) == S::RowsVerdict::kOk && rows[57] == 0, "RW-01 Fallout4.esm: ok, 57 free");
				std::uint64_t r2[64];
				std::memcpy(r2, rows, sizeof(r2));
				S::ApplyRows(r2, S::kLayProps);
				CHECK(r2[57] == S::kLayProps, "RW-01 row 57 = the loose props");
				bool sym = true, actor = false, other = false;
				for (int j = 0; j < 64; ++j) {
					sym = sym && (((r2[57] >> j) & 1ull) == ((r2[j] >> 57) & 1ull));
					actor = actor || (((r2[57] >> j) & 1ull) && ((S::kLayActors >> j) & 1ull));
					other = other || (j != 57 && ((r2[j] & ~(1ull << 57)) != rows[j]));
				}
				CHECK(sym && !actor && !other, "RW-01 symmetric, no actor layer (8 / 30 / 32 / 33), no other bit changed");
				CHECK(S::CheckRows(r2, S::kLayProps) == S::RowsVerdict::kOk, "RW-01 our own rows pass the check again (re-apply after a load)");
				std::uint64_t r3[64];
				std::memcpy(r3, r2, sizeof(r3));
				S::ApplyRows(r3, S::kLayProps);
				CHECK(std::memcmp(r2, r3, sizeof(r2)) == 0, "RW-01 idempotent");
				S::ApplyRows(r3, S::kLayProps | S::kLayCorpse);
				CHECK(((r3[32] >> 57) & 1ull) && ((r3[57] >> 32) & 1ull), "RW-01 pushers.corpses 1 adds the corpse layer");
				S::ApplyRows(r3, S::kLayProps);
				CHECK(!((r3[32] >> 57) & 1ull) && std::memcmp(r2, r3, sizeof(r2)) == 0, "RW-01 ... and takes it back exactly");
				std::uint64_t bad[64];
				std::memcpy(bad, rows, sizeof(bad));
				bad[4] ^= 1ull << 13;
				CHECK(S::CheckRows(bad, S::kLayProps) == S::RowsVerdict::kChanged, "RW-01 a changed esm row refuses");
				std::memcpy(bad, rows, sizeof(bad));
				bad[57] = 1ull << 1;
				CHECK(S::CheckRows(bad, S::kLayProps) == S::RowsVerdict::kTaken, "RW-01 layer 57 used by someone else refuses");
				std::memcpy(bad, rows, sizeof(bad));
				bad[1] |= 1ull << 57;
				CHECK(S::CheckRows(bad, S::kLayProps) == S::RowsVerdict::kTaken, "RW-01 bit 57 on a layer we never use refuses");
				// finB integrator: the game's matrix holds unused layers as ALL ONES (in game 10-27: row57 = FFFFFFFFFFFFFFFF) - free for us
				std::uint64_t live[64];
				std::memcpy(live, rows, sizeof(live));
				live[57] = ~0ull;
				live[60] = ~0ull;  // another unused layer, all ones too
				CHECK(S::CheckRows(live, S::kLayProps) == S::RowsVerdict::kOk, "RW-02 the game's unused-layer rows (all ones) are free");
				S::ApplyRows(live, S::kLayProps);
				bool symL = true, actorL = false;
				for (int j = 0; j < 64; ++j) {
					if (j == 60) continue;  // (an unused all-ones row keeps its bits; no body lives on it)
					symL = symL && (((live[57] >> j) & 1ull) == ((live[j] >> 57) & 1ull));
					actorL = actorL || (((live[57] >> j) & 1ull) && ((S::kLayActors >> j) & 1ull)) || (((S::kLayActors >> j) & 1ull) && ((live[j] >> 57) & 1ull));
				}
				CHECK(live[57] == S::kLayProps && symL && !actorL && S::CheckRows(live, S::kLayProps) == S::RowsVerdict::kOk,
					"RW-02 applied: row 57 = the loose props, symmetric, no actor layer, passes again after a load");
				std::memcpy(bad, live, sizeof(bad));
				bad[57] = (1ull << 30) | (1ull << 4);
				CHECK(S::CheckRows(bad, S::kLayProps) == S::RowsVerdict::kTaken, "RW-02 a partial row 57 with an actor layer is still someone else's");
			}
		}
		// ---- PR-01 the load-check table
		{
			std::map<std::uint64_t, int> ids;
			int impulse = 0, pushers = 0, groups = 0, callSites = 0;
			for (const auto& p : S::Proofs()) {
				ids[p.id]++;
				const std::string feat = p.feat;
				impulse += feat == "shove.impulse";
				pushers += feat == "shove.pushers";
				groups += feat == "shove.groups";
				if (p.off != 0 && std::string(p.hex).rfind("E8 ", 0) == 0) ++callSites;
				std::uint8_t b[32], any[32];
				CHECK(!p.hex || !p.hex[0] || sp::r6check::ParseHex(p.hex, b, any, 32) > 0, "PR-01 %s: the bytes parse", p.name);
			}
			for (const std::uint64_t id : { 2721632ull, 2721630ull, 2229991ull, 2255680ull, 2255688ull, 2255683ull, 2255689ull, 2255967ull, 2258369ull, 2258373ull, 2277676ull, 2255626ull,
					 2277684ull, 4797438ull, 2278061ull })
				CHECK(ids.count(id) == 1, "PR-01 the call %llu is checked at load", static_cast<unsigned long long>(id));
			CHECK(impulse == 3 && groups == 1 && pushers >= 15 && callSites == 4, "PR-01 features: impulse %d groups %d pushers %d, engine call sites %d", impulse, groups, pushers, callSites);
			// a wrong byte / a wrong RVA refuses exactly its feature
			auto good = [](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) {
				for (const auto& p : S::Proofs())
					if (p.id == a_id && p.off == a_off) {
						a_rva = p.rva;
						std::uint8_t any[32];
						sp::r6check::ParseHex(p.hex ? p.hex : "", a_out, any, a_n);
						return true;
					}
				return false;
			};
			const auto fAll = S::RunProofs(good, [](auto&&...) {});
			CHECK(fAll.impulse && fAll.pushers && fAll.groups, "PR-01 the exe's own bytes pass every proof");
			auto badKf = [&](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) {
				const bool r = good(a_id, a_rva, a_off, a_out, a_n);
				if (a_id == 2277676 && a_n > 0) a_out[0] ^= 0xFF;  // another plugin's detour on applyHardKeyFrame
				return r;
			};
			const auto fKf = S::RunProofs(badKf, [](auto&&...) {});
			CHECK(fKf.impulse && !fKf.pushers && fKf.groups, "PR-01 a detour on applyHardKeyFrame refuses only the pushers");
			auto badImp = [&](std::uint64_t a_id, std::uint32_t& a_rva, std::uint32_t a_off, std::uint8_t* a_out, int a_n) {
				const bool r = good(a_id, a_rva, a_off, a_out, a_n);
				if (a_id == 2721632) a_rva += 0x10;  // another Address Library
				return r;
			};
			const auto fImp = S::RunProofs(badImp, [](auto&&...) {});
			CHECK(!fImp.impulse && fImp.pushers, "PR-01 a moved applyBodyImpulseAt refuses only the impulses");
		}
		// ---- KY-15 code defaults and the shipped ini (as text)
		{
			const S::Cfg d{};
			CHECK(!d.on && !d.pushers && !d.foes && !d.pCorpses, "KY-15 the code defaults: every step-15 path off (the build before step 15)");
			std::map<std::string, double> ini;
			for (const char* p : { "SuperPeasantNative.ini", "../SuperPeasantNative.ini" }) {
				std::ifstream f(p);
				if (!f) continue;
				std::string line;
				while (std::getline(f, line)) {
					if (line.empty() || line[0] == ';' || line[0] == '[') continue;
					const auto eq = line.find('=');
					if (eq == std::string::npos) continue;
					std::string k = line.substr(0, eq);
					while (!k.empty() && (k.back() == ' ' || k.back() == '\t')) k.pop_back();
					ini[k] = std::atof(line.c_str() + eq + 1);
				}
				break;
			}
			CHECK(ini.count("look.bodyContacts") && ini["look.bodyContacts"] == 1.0, "KY-15 the shipped ini: look.bodyContacts = 1");
			// Experimental physical proxies are opt-in; the explicit on/off mechanics cases above remain required.
			CHECK(ini.count("look.pushers57") && ini["look.pushers57"] == 0.0, "KY-15 the shipped ini: look.pushers57 = 0");
			CHECK(!ini.count("look.shoveFoes") || ini["look.shoveFoes"] == 0.0, "KY-15 living foes are not shoved in the shipped ini");
			CHECK(!ini.count("pushers.corpses") || ini["pushers.corpses"] == 0.0, "KY-15 the pushers never meet a corpse in the shipped ini");
		}
		B2L3Units();
		FinishShoveUnits();
	}
}
