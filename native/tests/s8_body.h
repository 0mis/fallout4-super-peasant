#pragma once
// OWNER: TESTS lane, round 6 step 8. Ground truth on the REAL TABS body: its 16 colliders (15 capsules + 1 sphere, TABS
// tabs_constants.json via core/body_model.gen.h, scaled by the body size like Body::Build does) and the 8 fist boxes, rebuilt
// in the FO4 frame from Body::Part(i) (the solver's own pivots and rotations, Unity frame). Nothing here feeds the body: the
// cases use it to MEASURE (penetration of every part, hands / feet inside geometry, the torso inside a solid, snaps).
#include "core/body.h"
#include "core/body_model.gen.h"
#include "core/math.h"
#include "s8_world.h"

#include <array>

namespace s8
{
	struct BodyShapes
	{
		std::array<Shape, 24> s{};
		int n{ 0 };
	};

	inline Vec3 U2F(const Vec3& u, double upm) { return sp::math::UnityToFO4Pos(u, upm); }

	// every collider of the body in the FO4 frame (game units). a_fists: add the 8 fist boxes (4 per fist).
	inline BodyShapes Colliders(const sp::body::Body& a_b, bool a_fists = true)
	{
		BodyShapes   out;
		const double s = a_b.Size(), upm = a_b.Upm();
		namespace G = sp::body::gen;
		for (int i = 0; i < sp::body::kRigCount; ++i) {
			const auto& rd = G::kRigs[i];
			const auto& p = a_b.Part(i);
			for (int ci = 0; ci < rd.nCol; ++ci) {
				const auto&  cd = rd.col[ci];
				const Vec3   cen = p.pos + p.rot.Rotate(Vec3{ cd.c[0], cd.c[1], cd.c[2] } * s);
				const double rad = cd.r * s;
				Shape        sh;
				sh.rig = i;
				if (cd.kind == 0) {
					sh.kind = kSphere;
					sh.p0 = sh.p1 = U2F(cen, upm);
				} else {
					const double hh = std::max(0.0, cd.h * s / 2.0 - rad);
					const Vec3   ax = p.rot.Rotate(Normalize(Vec3{ cd.axis[0], cd.axis[1], cd.axis[2] }, { 0, 1, 0 }));
					sh.kind = kCapsule;
					sh.p0 = U2F(cen - ax * hh, upm);
					sh.p1 = U2F(cen + ax * hh, upm);
				}
				sh.r = rad * upm;
				if (out.n < static_cast<int>(out.s.size())) out.s[static_cast<size_t>(out.n++)] = sh;
			}
		}
		if (a_fists) {
			for (int f = 0; f < 2; ++f) {
				const int   rig = f == 0 ? sp::body::kFistL : sp::body::kFistR;
				const auto& p = a_b.Part(rig);
				for (int ci = 0; ci < G::kFistBoxes; ++ci) {
					const auto& bx = G::kFistBox[ci];
					const Quat  q = p.rot * sp::math::Normalize(Quat{ bx.rot[0], bx.rot[1], bx.rot[2], bx.rot[3] });
					Shape       sh;
					sh.rig = rig;
					sh.kind = kBox;
					sh.c = U2F(p.pos + p.rot.Rotate(Vec3{ bx.c[0], bx.c[1], bx.c[2] }), upm);
					sh.ax[0] = sp::math::UnityToFO4Dir(q.Rotate({ 1, 0, 0 }));
					sh.ax[1] = sp::math::UnityToFO4Dir(q.Rotate({ 0, 1, 0 }));
					sh.ax[2] = sp::math::UnityToFO4Dir(q.Rotate({ 0, 0, 1 }));
					sh.he = Vec3{ bx.he[0], bx.he[1], bx.he[2] } * upm;
					if (out.n < static_cast<int>(out.s.size())) out.s[static_cast<size_t>(out.n++)] = sh;
				}
			}
		}
		return out;
	}

	inline Vec3 PartF(const sp::body::Body& b, int rig) { return U2F(b.Part(rig).pos, b.Upm()); }
	// the distance (u) from a rig's pivot (what Part(i).pos is) to its centre of mass (what Part(i).vel is the velocity of):
	// a pivot moves up to |omega| x this faster than the reported velocity (the snap meter's allowance)
	inline double ComArmU(int rig, double s, double upm)
	{
		namespace G = sp::body::gen;
		if (rig >= sp::body::kRigCount) {
			Vec3 m{};
			for (int i = 0; i < G::kFistBoxes; ++i) m = m + Vec3{ G::kFistBox[i].c[0], G::kFistBox[i].c[1], G::kFistBox[i].c[2] };
			return Length(m * (1.0 / G::kFistBoxes)) * upm + 4.0;
		}
		const auto& rd = G::kRigs[rig];
		if (rd.hasCom) return Length(Vec3{ rd.com[0], rd.com[1], rd.com[2] }) * s * upm + 4.0;
		Vec3 m{};
		for (int i = 0; i < rd.nCol; ++i) m = m + Vec3{ rd.col[i].c[0], rd.col[i].c[1], rd.col[i].c[2] };
		return (rd.nCol > 0 ? Length(m * (1.0 / rd.nCol)) * s * upm : 0.0) + 4.0;
	}
	// the foot: the centre of the knee rig's first collider (the foot capsule, TABS Knee col[0])
	inline Vec3 FootF(const sp::body::Body& b, int side)
	{
		const int   rig = side == 0 ? sp::body::kKneeL : sp::body::kKneeR;
		const auto& cd = sp::body::gen::kRigs[rig].col[0];
		const auto& p = b.Part(rig);
		return U2F(p.pos + p.rot.Rotate(Vec3{ cd.c[0], cd.c[1], cd.c[2] } * b.Size()), b.Upm());
	}
	inline Vec3 VelF(const sp::body::Body& b, int rig) { return sp::math::UnityToFO4Dir(b.Part(rig).vel) * b.Upm(); }
	// the centre of mass speed proxy the runtime uses (hip + torso mean, u/s)
	inline Vec3 CoreVelF(const sp::body::Body& b) { return (VelF(b, sp::body::kHip) + VelF(b, sp::body::kTorso)) * 0.5; }

	// limb classes for the "hands / feet inside geometry" mark and the per-class depths
	enum class Limb : int { kCore = 0, kHead = 1, kArm = 2, kHand = 3, kLeg = 4, kFoot = 5 };
	inline Limb LimbOf(int rig)
	{
		using namespace sp::body;
		switch (rig) {
		case kHead: return Limb::kHead;
		case kHip:
		case kTorso: return Limb::kCore;
		case kArmL:
		case kArmR: return Limb::kArm;
		case kElbowL:
		case kElbowR:
		case kFistL:
		case kFistR: return Limb::kHand;  // the elbow rig carries the hand (pivot at the hand, spec 4.8)
		case kLegL:
		case kLegR: return Limb::kLeg;
		default: return Limb::kFoot;      // the knee rigs carry the shin + foot
		}
	}
}
