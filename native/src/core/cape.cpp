// OWNER: STEP 7.6 (One Punch Man look). See cape.h.
#include "core/cape.h"

#include "core/cape_model.gen.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sp::cape
{
	using namespace sp::math;
	static_assert(model::kU == kU && model::kV == kV && model::kBones == kBones);

	namespace
	{
		Xform BindOf(const model::Bind& b)
		{
			Xform x;
			for (int r = 0; r < 3; ++r)
				for (int c = 0; c < 3; ++c) x.R.m[r][c] = b.R[r * 3 + c];
			x.t = { b.T[0], b.T[1], b.T[2] };
			x.s = 1;
			return x;
		}
		bool Fin(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
		int  Idx(int i, int j) { return j * kU + i; }

		struct Link
		{
			std::uint8_t a, b;
			float        rest;  // model units
			std::uint8_t kind;  // 0 stretch, 1 shear, 2 bend
		};
		std::array<Link, 512> g_links{};
		int                   g_nLinks = 0;

		void BuildLinks()
		{
			if (g_nLinks) return;
			auto add = [](int i0, int j0, int i1, int j1, int kind) {
				if (i1 < 0 || i1 >= kU || j1 < 0 || j1 >= kV) return;
				// Preserve the existing edge links without reading the next row through an out-of-range column.
				if (i0 >= kU && j0 + 1 < kV) {
					i0 -= kU;
					++j0;
				}
				if (i0 < 0 || i0 >= kU || j0 < 0 || j0 >= kV) return;
				const Vec3 a = Cloth::RestModel(i0, j0), b = Cloth::RestModel(i1, j1);
				g_links[g_nLinks++] = { static_cast<std::uint8_t>(Idx(i0, j0)), static_cast<std::uint8_t>(Idx(i1, j1)), static_cast<float>(Length(a - b)),
					static_cast<std::uint8_t>(kind) };
			};
			for (int j = 0; j < kV; ++j)
				for (int i = 0; i < kU; ++i) {
					add(i, j, i + 1, j, 0);
					add(i, j, i, j + 1, 0);
					add(i, j, i + 1, j + 1, 1);
					add(i + 1, j, i, j + 1, 1);
					add(i, j, i + 2, j, 2);
					add(i, j, i, j + 2, 2);
				}
		}
	}

	Vec3 Cloth::RestModel(int i, int j) { return { model::kRest[j][i][0], model::kRest[j][i][1], model::kRest[j][i][2] }; }
	Xform Cloth::BoneBind(int k) { return BindOf(model::kBoneBind[k]); }
	void Cloth::BoneUV(int k, Real& u, Real& v)
	{
		u = model::kBoneU[k];
		v = model::kBoneV[k];
	}

	Cloth::Cloth()
	{
		BuildLinks();
		// rest geodesic of every particle to its column's pin: the sum of vertical rest segments
		for (int i = 0; i < kU; ++i) {
			Real acc = 0;
			_lraRest[Idx(i, 0)] = 0;
			for (int j = 1; j < kV; ++j) {
				_vRest[Idx(i, j)] = Length(RestModel(i, j) - RestModel(i, j - 1));
				acc += _vRest[Idx(i, j)];
				_lraRest[Idx(i, j)] = acc;
			}
		}
		for (int j = 0; j < kV; ++j)
			for (int i = 0; i + 1 < kU; ++i) _hRest[Idx(i, j)] = Length(RestModel(i + 1, j) - RestModel(i, j));
		// bone frames at the bind, from the rest grid (model space) exactly like at runtime
		for (int j = 0; j < kV; ++j)
			for (int i = 0; i < kU; ++i) _x[Idx(i, j)] = RestModel(i, j);
		for (int k = 0; k < kBones; ++k) {
			Real u, v;
			BoneUV(k, u, v);
			FrameAt(u * (kU - 1), v * (kV - 1), _Pbind[k], _Rbind[k]);
		}
		_x.fill({});
	}

	void Cloth::Reset(const Xform& a_pinFrame, Real a_scale)
	{
		_scale = a_scale > 0 ? a_scale : 1;
		_pinFrame = a_pinFrame;
		for (int j = 0; j < kV; ++j)
			for (int i = 0; i < kU; ++i) {
				const Vec3 p = a_pinFrame.Apply(RestModel(i, j));
				_x[Idx(i, j)] = p;
				_xp[Idx(i, j)] = p;
			}
		for (int i = 0; i < kU; ++i) _pinPrev[i] = _pinNow[i] = _x[Idx(i, 0)];
		_bonePosValid = false;
		_ready = true;
		++_stats.resets;
	}

	Vec3 Cloth::Sample(Real fi, Real fj) const
	{
		fi = std::clamp(fi, Real(0), Real(kU - 1));
		fj = std::clamp(fj, Real(0), Real(kV - 1));
		const int  i0 = std::min(kU - 2, static_cast<int>(fi)), j0 = std::min(kV - 2, static_cast<int>(fj));
		const Real a = fi - i0, b = fj - j0;
		return _x[Idx(i0, j0)] * ((1 - a) * (1 - b)) + _x[Idx(i0 + 1, j0)] * (a * (1 - b)) + _x[Idx(i0, j0 + 1)] * ((1 - a) * b) + _x[Idx(i0 + 1, j0 + 1)] * (a * b);
	}

	void Cloth::FrameAt(Real fi, Real fj, Vec3& o_pos, Mat3& o_R) const
	{
		o_pos = Sample(fi, fj);
		const Real e = 0.5;
		const Vec3 du = Sample(fi + e, fj) - Sample(fi - e, fj);
		const Vec3 dv = Sample(fi, fj + e) - Sample(fi, fj - e);
		const Vec3 e1 = Normalize(du, { 1, 0, 0 });
		Vec3       n = Normalize(Cross(du, dv), { 0, -1, 0 });
		Vec3       e2 = Cross(n, e1);
		e2 = Normalize(e2, { 0, 0, -1 });
		n = Cross(e1, e2);
		for (int r = 0; r < 3; ++r) {
			o_R.m[r][0] = e1[r];
			o_R.m[r][1] = e2[r];
			o_R.m[r][2] = n[r];
		}
	}

	void Cloth::Constrain(const Input& a_in, const Params& a_p, Real w)
	{
		const Real s = _scale;
		const Vec3 fwd = Normalize(a_in.pinFrame.R * Vec3{ 0, 1, 0 }, { 0, 1, 0 });
		const Vec3 up = Normalize(a_in.pinFrame.R * Vec3{ 0, 0, 1 }, { 0, 0, 1 });
		for (int l = 0; l < g_nLinks; ++l) {
			const Link& k = g_links[l];
			if (k.kind == 2 && !_iterFirst) continue;  // bending once per sub-step (soft anyway)
			const Real  stiff = k.kind == 0 ? a_p.stretch : (k.kind == 1 ? a_p.shear : a_p.bend);
			Vec3&       pa = _x[k.a];
			Vec3&       pb = _x[k.b];
			const bool  fa = k.a < kU, fb = k.b < kU;  // row 0 is pinned
			if (fa && fb) continue;
			const Vec3 d = pb - pa;
			const Real len = Length(d);
			if (len < 1e-9) continue;
			const Real rest = k.rest * s;
			Real       diff = (len - rest) / len;
			if (k.kind == 2 && diff < 0) diff *= 0.3;  // bending resists stretching more than folding (cloth folds easily)
			const Vec3 corr = d * (diff * stiff * w);
			if (fa) pb -= corr;
			else if (fb) pa += corr;
			else {
				pa += corr * 0.5;
				pb -= corr * 0.5;
			}
		}
		Inextensible(a_p);
		if (!_iterLast) return;  // the hard limits (attachments, cone) once per sub-step, before the last collision pass
		// long-range attachments: never farther from its column's pin than the rest geodesic (x lra)
		for (int j = 1; j < kV; ++j)
			for (int i = 0; i < kU; ++i) {
				Vec3&      p = _x[Idx(i, j)];
				const Vec3 d = p - _x[Idx(i, 0)];
				const Real len = Length(d);
				const Real mx = _lraRest[Idx(i, j)] * s * a_p.lra;
				if (len > mx && len > 1e-9) {
					const Vec3 np = _x[Idx(i, 0)] + d * (mx / len);
					_xp[Idx(i, j)] += (np - p) * a_p.carry;
					p = np;
				}
			}
		// cape cone: seen from its pin, a particle never goes in front of his shoulder plane (+ frontLimit) nor high over
		// his shoulders (upLimit): no flip over his head to the front on a dead stop, nothing to wrap through him
		if (a_p.frontLimit < 1.0 || a_p.upLimit < 1.0)
			for (int j = 1; j < kV; ++j)
				for (int i = 0; i < kU; ++i) {
					Vec3&      p = _x[Idx(i, j)];
					const Vec3 pin = _x[Idx(i, 0)];
					Vec3       d = p - pin;
					const Real len = Length(d);
					if (len < 1e-6) continue;
					Vec3       u = d / len;
					bool       moved = false;
					if (const Real fu = Dot(u, fwd); fu > a_p.frontLimit) {
						u = u - fwd * (fu - a_p.frontLimit);
						moved = true;
					}
					if (const Real uu = Dot(u, up); uu > a_p.upLimit) {
						u = u - up * (uu - a_p.upLimit);
						moved = true;
					}
					if (!moved) continue;
					const Vec3 np = pin + Normalize(u, -up) * len;
					_xp[Idx(i, j)] += (np - p) * a_p.carry;
					p = np;
				}
		Inextensible(a_p);  // the limits above may have pulled rows apart: inextensible again (last word before collisions)
	}

	void Cloth::Inextensible(const Params& a_p)
	{
		const Real s = _scale;
		// follow-the-leader down every column (top -> hem): a vertical link never stretches past 3 % (inextensible cape)
		for (int j = 1; j < kV; ++j)
			for (int i = 0; i < kU; ++i) {
				Vec3&      p = _x[Idx(i, j)];
				const Vec3 d = p - _x[Idx(i, j - 1)];
				const Real len = Length(d);
				const Real mx = _vRest[Idx(i, j)] * s * 1.03;
				if (len > mx && len > 1e-9) {
					const Vec3 np = _x[Idx(i, j - 1)] + d * (mx / len);
					_xp[Idx(i, j)] += (np - p) * a_p.carry;  // dragged along WITHOUT gaining that speed (no whip energy)
					p = np;
				}
			}
		// width: a row link never stretches past 5 % (both ends move)
		for (int j = 1; j < kV; ++j)
			for (int i = 0; i + 1 < kU; ++i) {
				Vec3&      pa = _x[Idx(i, j)];
				Vec3&      pb = _x[Idx(i + 1, j)];
				const Vec3 d = pb - pa;
				const Real len = Length(d);
				const Real mx = _hRest[Idx(i, j)] * s * 1.05;
				if (len > mx && len > 1e-9) {
					const Vec3 corr = d * (0.5 * (len - mx) / len);
					pa += corr;
					pb -= corr;
					_xp[Idx(i, j)] += corr * a_p.carry;
					_xp[Idx(i + 1, j)] -= corr * a_p.carry;
				}
			}
	}

	void Cloth::Collide(const Input& a_in, const Params& a_p, bool a_friction)
	{
		int contacts = 0;
		for (int j = 1; j < kV; ++j)
			for (int i = 0; i < kU; ++i) {
				const int id = Idx(i, j);
				Vec3&     p = _x[id];
				bool      hit = false;
				for (int c = 0; c < a_in.nCaps; ++c) {
					const Capsule& cp = a_in.caps[c];
					const Vec3     ab = cp.b - cp.a;
					const Real     l2 = LengthSq(ab);
					const Real     t = l2 > 1e-9 ? std::clamp(Dot(p - cp.a, ab) / l2, Real(0), Real(1)) : Real(0);
					const Vec3     q = cp.a + ab * t;
					const Vec3     d = p - q;
					const Real     R = cp.r + a_p.margin * _scale;
					const Real     dl = Length(d);
					if (LengthSq(cp.fwd) > 0.5) {
						// one-sided torso / belt: an elliptic tube (half-width w, back depth r, front depth 1.2 r). A particle
						// inside is pushed straight BACK (along -his forward) onto the back surface keeping its side
						// coordinate - never out the front, never sideways (neighbours stay together) - unless it was in
						// front of him already last sub-step (the cape blown forward over him): then it stays in front.
						const Vec3 ax = l2 > 1e-9 ? ab / std::sqrt(l2) : Vec3{ 0, 0, 1 };
						const Vec3 f = Normalize(cp.fwd - ax * Dot(cp.fwd, ax), cp.fwd);
						const Vec3 side = Cross(ax, f);
						const Real W = (cp.w > 0 ? cp.w : cp.r) + a_p.margin * _scale;
						const Real ad = Dot(d, ax), sd = Dot(d, side), fd = Dot(d, f);
						const Real e = (sd / W) * (sd / W) + (ad / R) * (ad / R);
						if (e >= 1) continue;
						const bool front = false;  // the cape cone keeps it behind his shoulder plane: always out the back
						const Real lim = (front ? 1.2 * R : R) * std::sqrt(1 - e);
						if (front) {
							if (fd >= lim) continue;
							p += f * (lim - fd);
						} else {
							if (fd <= -lim) continue;
							p -= f * (fd + lim);
						}
						hit = true;
						continue;
					}
					if (dl >= R) continue;
					hit = true;
					if (dl < 1e-6) p = q + Vec3{ 0, 0, R };
					else p = q + d * (R / dl);
				}
				if (p.z < a_in.groundZ + a_p.margin * _scale) {
					p.z = a_in.groundZ + a_p.margin * _scale;
					hit = true;
				}
				if (hit) {
					++contacts;
					if (a_friction) {  // contact friction: lose part of the tangential motion of this sub-step
						Vec3 v = p - _xp[id];
						_xp[id] += v * a_p.friction;
					}
				}
			}
		_stats.contacts = contacts;
	}

	void Cloth::SubStep(const Input& a_in, const Params& a_p, Real h, Real a_tFrac)
	{
		// pins: interpolate the pinned row between last and this frame (no snap on long frames)
		for (int i = 0; i < kU; ++i) {
			const Vec3 pin = _pinPrev[i] + (_pinNow[i] - _pinPrev[i]) * a_tFrac;
			_xp[Idx(i, 0)] = _x[Idx(i, 0)];
			_x[Idx(i, 0)] = pin;
		}
		const Vec3 g{ 0, 0, -a_p.gravity };
		const Real phase = _t * a_p.flutterHz * 2.0 * kPi;
		// s12 r3 lane 5 (cape.breeze): the air the drag acts against - a world-space breeze in slow gusts (0.25..1.0 of breeze) whose
		// direction veers +-30 deg, plus a ripple running across the sheet (folds); zero = still air (bit exact)
		Vec3 wind{};
		Real windPh = 0;
		const bool breeze = a_p.breeze > 0 && std::isfinite(a_p.breeze);
		if (breeze) {
			const Real w = _t * std::clamp(a_p.breezeHz, Real(0.01), Real(2.0)) * 2.0 * kPi;
			const Real gust = 0.62 + 0.26 * std::sin(w) + 0.12 * std::sin(2.7 * w + 1.3);
			const Real ang = 0.9 + 0.52 * std::sin(0.37 * w + 0.4);
			wind = Vec3{ std::cos(ang), std::sin(ang), 0.0 } * (std::min(a_p.breeze, Real(600)) * gust);
			windPh = 3.1 * w;
		}
		for (int j = 1; j < kV; ++j)
			for (int i = 0; i < kU; ++i) {
				const int id = Idx(i, j);
				Vec3      v = (_x[id] - _xp[id]) / h;
				// particle normal from its grid neighbours
				const Vec3 du = _x[Idx(std::min(i + 1, kU - 1), j)] - _x[Idx(std::max(i - 1, 0), j)];
				const Vec3 dv = _x[Idx(i, std::min(j + 1, kV - 1))] - _x[Idx(i, j - 1)];
				const Vec3 n = Normalize(Cross(du, dv), { 0, 0, 0 });
				// the drag acts on the velocity relative to the air (still air: the particle's own velocity)
				const Vec3 vr = breeze ? v - wind * (1.0 + 0.3 * std::sin(windPh - j * 0.7 + (i - 2.5) * 0.6)) : v;
				const Real vn = Dot(vr, n);
				Vec3       a = g - vr * a_p.airDrag - n * (a_p.normalDrag * vn * std::abs(vn));
				// travelling flutter: grows with speed, a wave running down the cape (phase by row), opposite edges out of step
				const Real sp = Length(v);
				if (a_p.flutter > 0 && sp > 50) {
					const Real amp = std::min(Real(1), sp / a_p.flutterSpeed);
					const Real wave = std::sin(phase - j * 0.9 + (i - 2.5) * 0.35);
					a += n * (a_p.flutter * a_p.normalDrag * sp * sp * amp * wave * (static_cast<Real>(j) / (kV - 1)));
				}
				Vec3 step = (_x[id] - _xp[id]) * (1.0 - a_p.damping) + a * (h * h);
				// displacement cap relative to the chest motion (never explode)
				const Real cap = a_p.maxSpeed * h;
				const Real sl = Length(step);
				if (sl > cap && sl > 1e-9) step = step * (cap / sl);
				_xp[id] = _x[id];
				_x[id] += step;
			}
		for (int it = 0; it < a_p.iters; ++it) {
			_iterLast = it == a_p.iters - 1;
			_iterFirst = it == 0;
			if (_iterLast) {
				Collide(a_in, a_p, true);   // last pass: collide, then the hard length limits get the last word (a stretched
				Constrain(a_in, a_p, 1.0);  // cape shows; a sub-margin graze does not, and the next sub-step pushes it out)
			} else {
				Constrain(a_in, a_p, 1.0);
				if (it >= a_p.iters - 3) Collide(a_in, a_p, false);
			}
		}
		_t += h;
	}

	bool Cloth::Step(const Input& a_in, const Params& a_p)
	{
		const Real dt = std::clamp(a_in.dt, Real(1e-4), Real(0.1));
		_lastDt = dt;
		if (!_ready || std::abs(a_in.scale - _scale) > 1e-3) Reset(a_in.pinFrame, a_in.scale);
		_scale = a_in.scale > 0 ? a_in.scale : 1;
		_pinFrame = a_in.pinFrame;
		Real jump = 0;
		for (int i = 0; i < kU; ++i) {
			_pinPrev[i] = _x[Idx(i, 0)];
			_pinNow[i] = a_in.pinFrame.Apply(RestModel(i, 0));
			jump = std::max(jump, Length(_pinNow[i] - _pinPrev[i]));
		}
		if (!Fin(_pinNow[0]) || jump > a_p.teleport * std::max(Real(1), dt * 60.0)) {
			++_stats.teleportResets;
			Reset(a_in.pinFrame, a_in.scale);
			return true;
		}
		const int n = std::clamp(std::max(a_p.substeps, static_cast<int>(std::ceil(dt * a_p.subHz - 0.05))), 1, std::max(1, a_p.maxSub));  // sub-step <= 1/subHz s, at most maxSub (cost cap)
		const Real h = dt / n;
		for (int s = 0; s < n; ++s) SubStep(a_in, a_p, h, static_cast<Real>(s + 1) / n);
		++_stats.steps;
		// sanity: non-finite or blown up -> rest drape
		bool bad = false;
		Real worst = 0;
		for (int k = 0; k < kN; ++k) {
			if (!Fin(_x[k])) {
				bad = true;
				break;
			}
			worst = std::max(worst, Length(_x[k] - _x[0]));
		}
		if (bad || worst > 400.0 * _scale) {
			++_stats.nanResets;
			Reset(a_in.pinFrame, a_in.scale);
			return false;
		}
		// stats: strain, depth inside capsules (every 4th step: diagnostics only)
		if ((_stats.steps & 7) != 0) return true;
		for (int l = 0; l < g_nLinks; ++l)
			if (g_links[l].kind == 0) {
				const Real len = Length(_x[g_links[l].b] - _x[g_links[l].a]);
				const Real st = len / std::max(Real(1e-6), g_links[l].rest * _scale) - 1.0;
				if (st > _stats.maxStretch) {
					_stats.maxStretch = st;
					_stats.worstLinkA = g_links[l].a;
					_stats.worstLinkB = g_links[l].b;
				}
			}
		for (int k = kU; k < kN; ++k)
			for (int c = 0; c < a_in.nCaps; ++c) {
				const Capsule& cp = a_in.caps[c];
				const Vec3     ab = cp.b - cp.a;
				const Real     l2 = LengthSq(ab);
				const Real     t = l2 > 1e-9 ? std::clamp(Dot(_x[k] - cp.a, ab) / l2, Real(0), Real(1)) : Real(0);
				const Real     depth = cp.r - Length(_x[k] - (cp.a + ab * t));
				_stats.maxInside = std::max(_stats.maxInside, depth);
			}
		return true;
	}

	void Cloth::BoneWorlds(std::array<Xform, kBones>& a_out) const
	{
		const Real s = _scale;
		for (int k = 0; k < kBones; ++k) {
			Real u, v;
			BoneUV(k, u, v);
			Vec3 X;
			Mat3 Rn;
			FrameAt(u * (kU - 1), v * (kV - 1), X, Rn);
			const Mat3 Rw = Rn * _Rbind[k].Transposed();
			Xform      A;
			A.R = Rw;
			A.s = s;
			A.t = X - Rw * (_Pbind[k] * s);
			a_out[k] = Compose(A, BoneBind(k));
		}
		// pop / jitter stat: acceleration of the bone anchors (frame to frame)
		auto* self = const_cast<Cloth*>(this);
		const Real dt = std::max(Real(1e-4), _lastDt);
		// the pinned row's own acceleration (his chest): a pop is cape motion that is NOT his
		const Vec3 pc = (_x[Idx(2, 0)] + _x[Idx(3, 0)]) * 0.5;
		Vec3       pinAcc{};
		if (_bonePosValid) {
			const Vec3 pv = (pc - _pinC) / dt;
			pinAcc = (pv - _pinV) / dt;
			self->_pinV = pv;
		} else {
			self->_pinV = {};
		}
		self->_pinC = pc;
		for (int k = 0; k < kBones; ++k) {
			Real u, v;
			BoneUV(k, u, v);
			const Vec3 X = Sample(u * (kU - 1), v * (kV - 1));
			if (_bonePosValid) {
				const Vec3 vel = (X - _bonePos[k]) / dt;
				const Vec3 acc = (vel - _boneVel[k]) / dt;
				self->_stats.maxBoneAccel = std::max(self->_stats.maxBoneAccel, Length(acc));
				self->_stats.maxPop = std::max(self->_stats.maxPop, Length(acc - pinAcc));
				self->_boneVel[k] = vel;
			} else {
				self->_boneVel[k] = {};
			}
			self->_bonePos[k] = X;
		}
		self->_bonePosValid = true;
	}

	Stats Cloth::TakeStats()
	{
		Stats s = _stats;
		_stats.maxStretch = 0;
		_stats.maxBoneAccel = 0;
		_stats.maxPop = 0;
		_stats.maxInside = 0;
		return s;
	}

	bool g_armCaps = false;
	void SetArmCapsules(bool a_on) { g_armCaps = a_on; }
	bool ArmCapsules() { return g_armCaps; }

	int RestCapsules(const Xform& a_bodyFrame, Real a_scale, Capsule* a_out, int a_max)
	{
		int n = 0;
		for (int c = 0; c < model::kCaps && n < a_max; ++c) {
			const auto& d = model::kCap[c];
			if (!g_armCaps && std::strstr(d.bone, "Arm_")) continue;
			Capsule&    o = a_out[n++];
			o.a = a_bodyFrame.Apply({ d.p0[0], d.p0[1], d.p0[2] });
			o.b = a_bodyFrame.Apply({ d.p1[0], d.p1[1], d.p1[2] });
			o.r = d.r * a_scale;
			o.w = d.w * a_scale;
			o.fwd = d.oneSided ? Normalize(a_bodyFrame.R * Vec3{ 0, 1, 0 }, { 0, 1, 0 }) : Vec3{};
		}
		return n;
	}
}
