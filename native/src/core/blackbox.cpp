// OWNER: R6 (round 6 step 2). See blackbox.h.
#include "core/blackbox.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <type_traits>

namespace sp::bb
{
	static_assert(std::is_trivially_copyable_v<Facts>, "Facts is recorded raw");
	static_assert(std::is_trivially_copyable_v<Cand>, "Cand is recorded raw");
	static_assert(std::is_trivially_copyable_v<Params>, "Params is recorded raw");
	static_assert(std::is_trivially_copyable_v<DecisionKey>);
	static_assert(std::is_copy_constructible_v<Brain>, "the keyframe is a copy of the brain");

	constexpr std::uint8_t kExtParams = 10;  // inline Params change (raw Params)
	constexpr std::uint32_t kFrameMagic = 0x314D5246u;  // "FRM1"

	namespace
	{
		std::uint64_t Fnv(const void* a_p, std::size_t a_n, std::uint64_t h = 1469598103934665603ull)
		{
			const auto* p = static_cast<const std::uint8_t*>(a_p);
			for (std::size_t i = 0; i < a_n; ++i) h = (h ^ p[i]) * 1099511628211ull;
			return h;
		}

		// zero-run packing of a_x (the XOR of a record with the previous one): [u32 rawLen] then pairs
		// [u16 zeros][u16 literal][literal bytes] until rawLen is covered.
		void Pack(const std::vector<std::uint8_t>& a_x, std::vector<std::uint8_t>& a_out)
		{
			const std::uint32_t n = static_cast<std::uint32_t>(a_x.size());
			const auto*         p = a_x.data();
			auto put16 = [&](std::uint16_t v) { a_out.push_back(static_cast<std::uint8_t>(v & 0xFF)); a_out.push_back(static_cast<std::uint8_t>(v >> 8)); };
			a_out.insert(a_out.end(), reinterpret_cast<const std::uint8_t*>(&n), reinterpret_cast<const std::uint8_t*>(&n) + 4);
			std::uint32_t i = 0;
			while (i < n) {
				std::uint32_t z = 0;
				// integrator (cost): skip zero bytes 8 at a time first (the XOR of two frames is mostly zeros); the same
				// output as the byte loop alone (z still stops at the first non-zero byte or at 0xFFFF)
				for (std::uint64_t w = 0; i + z + 8 <= n && z + 8 <= 0xFFFF; z += 8) {
					std::memcpy(&w, p + i + z, 8);
					if (w != 0) break;
				}
				while (i + z < n && p[i + z] == 0 && z < 0xFFFF) ++z;
				i += z;
				std::uint32_t lit = 0;
				while (i + lit < n && lit < 0xFFFF) {
					// stop the literal at a run of >= 4 zeros
					if (p[i + lit] == 0) {
						std::uint32_t zz = 0;
						while (i + lit + zz < n && p[i + lit + zz] == 0 && zz < 4) ++zz;
						if (zz >= 4 || i + lit + zz >= n) break;
					}
					++lit;
				}
				put16(static_cast<std::uint16_t>(z));
				put16(static_cast<std::uint16_t>(lit));
				a_out.insert(a_out.end(), p + i, p + i + lit);
				i += lit;
			}
		}

		bool Unpack(Reader& r, std::vector<std::uint8_t>& a_x)
		{
			std::uint32_t n = 0;
			if (!r.Get(n) || n > (64u << 20)) return false;
			a_x.assign(n, 0);
			std::uint32_t i = 0;
			while (i < n) {
				std::uint16_t z = 0, lit = 0;
				if (!r.Get(z) || !r.Get(lit)) return false;
				if (static_cast<std::uint64_t>(i) + z + lit > n) return false;
				i += z;
				if (lit && !r.GetRaw(a_x.data() + i, lit)) return false;
				i += lit;
				if (z == 0 && lit == 0) return false;  // no progress: corrupt
			}
			return true;
		}

		void XorInto(std::vector<std::uint8_t>& a_x, const std::vector<std::uint8_t>& a_base)
		{
			const std::size_t m = std::min(a_x.size(), a_base.size());
			// step-2 fixer (COST): 8 bytes at a time (same result as the byte loop)
			std::size_t i = 0;
			for (; i + 8 <= m; i += 8) {
				std::uint64_t x, b;
				std::memcpy(&x, a_x.data() + i, 8);
				std::memcpy(&b, a_base.data() + i, 8);
				x ^= b;
				std::memcpy(a_x.data() + i, &x, 8);
			}
			for (; i < m; ++i) a_x[i] ^= a_base[i];
		}

		bool DecodeRecord(const std::vector<std::uint8_t>& a_raw, Frame& a_f)
		{
			Reader r(a_raw.data(), a_raw.size());
			std::uint32_t magic = 0;
			if (!r.Get(magic) || magic != kFrameMagic) return false;
			r.Get(a_f.wall);
			r.GetRaw(&a_f.facts, sizeof(Facts));
			std::uint32_t nc = 0;
			if (!r.Get(nc) || nc > 4096) return false;
			a_f.cands.resize(nc);
			if (nc) r.GetRaw(a_f.cands.data(), sizeof(Cand) * nc);
			a_f.facts.cands = std::span<const Cand>(a_f.cands.data(), a_f.cands.size());
			r.Get(a_f.paramsHash);
			std::uint32_t ne = 0, nt = 0;
			if (!r.Get(ne) || ne > r.Left()) return false;
			a_f.ext.resize(ne);
			if (ne) r.GetRaw(a_f.ext.data(), ne);
			if (!r.Get(nt) || nt > r.Left()) return false;
			a_f.tape.resize(nt);
			if (nt) r.GetRaw(a_f.tape.data(), nt);
			r.Get(a_f.dec);
			// R6 step 4: the Warden's record of this frame (absent in a frame recorded without it)
			a_f.wrec.clear();
			if (r.Ok() && r.Left() >= 4) {
				std::uint32_t nw = 0;
				if (!r.Get(nw) || nw > r.Left()) return false;
				a_f.wrec.resize(nw);
				if (nw) r.GetRaw(a_f.wrec.data(), nw);
			}
			return r.Ok();
		}

		std::uint32_t Flags(const Out& o)
		{
			std::uint32_t f = 0;
			auto b = [&](bool v, int bit) { if (v) f |= 1u << bit; };
			b(o.visible, 0); b(o.mayAttack, 1); b(o.pin, 2); b(o.blink, 3); b(o.putBack, 4); b(o.huntStart, 5); b(o.huntAttack, 6); b(o.huntEnd, 7);
			b(o.returnStart, 8); b(o.chaseRequest, 9); b(o.vetRequest, 10); b(o.targetChanged, 11); b(o.climb, 12); b(o.launch, 13); b(o.directClear, 14);
			b(o.blinkAttack, 15);
			b(o.reachFail, 16);  // R6 step 4 (Facts::ext.on: a give-up reported as a reach failure)
			b(o.teleShieldId != 0, 17);  // R6 step 10 (Facts::tele.on: the Pilot's shield case)
			return f;
		}
	}

	DecisionKey KeyOf(const Out& o)
	{
		DecisionKey k;
		k.target = o.target;
		k.tier = static_cast<std::int32_t>(o.tier);
		k.cls = static_cast<std::int32_t>(o.cls);
		k.move = static_cast<std::int32_t>(o.move);
		k.hunt = static_cast<std::int32_t>(o.hunt);
		k.blinkWhy = o.blinkWhy;
		k.putBackWhy = static_cast<std::int32_t>(o.putBackWhy);
		k.giveUp = static_cast<std::int32_t>(o.giveUp);
		k.reachStage = o.reachStage;
		k.planKind = o.planKind;
		k.rays = o.rays;
		k.giveUpId = o.giveUpId;
		k.huntEndId = o.huntEndId;
		k.flags = Flags(o);
		k.dist = o.dist;
		k.dvMax = o.dvMax;
		k.vMax = o.vMax;
		k.aim = o.aim;
		k.vDes = o.vDes;
		k.face = o.face;
		k.blinkFeet = o.blinkFeet;
		return k;
	}
	std::uint64_t HashOf(const DecisionKey& a_k) { return Fnv(&a_k, sizeof(a_k)); }
	std::uint64_t HashParams(const Params& a_p) { return Fnv(&a_p, sizeof(a_p)); }

	void Bytes::PutStr(const char* a_s)
	{
		const std::uint32_t n = a_s ? static_cast<std::uint32_t>(std::min<std::size_t>(std::strlen(a_s), 255)) : 0;
		Put(n);
		if (n) PutRaw(a_s, n);
	}
	bool Reader::GetStr(std::string& a_out)
	{
		std::uint32_t n = 0;
		if (!Get(n) || n > 4096) return Fail();
		a_out.resize(n);
		return n == 0 || GetRaw(a_out.data(), n);
	}

	// ------------------------------------------------------------------------------------------------ RecordingWorld
	bool RecordingWorld::ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps)
	{
		const bool r = _w ? _w->ClearTo(a_from, a_to, a_targetId, a_reachEps) : false;
		if (_t) { _t->Put(Call::kClearTo); _t->Put(a_from); _t->Put(a_to); _t->Put(a_targetId); _t->Put(a_reachEps); _t->Put(r); }
		++_n;
		return r;
	}
	bool RecordingWorld::Clear(const Vec3& a_from, const Vec3& a_to)
	{
		const bool r = _w ? _w->Clear(a_from, a_to) : false;
		if (_t) { _t->Put(Call::kClear); _t->Put(a_from); _t->Put(a_to); _t->Put(r); }
		++_n;
		return r;
	}
	bool RecordingWorld::Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor)
	{
		const bool r = _w ? _w->Floor(a_from, a_maxDrop, a_outFloor) : false;
		if (_t) { _t->Put(Call::kFloor); _t->Put(a_from); _t->Put(a_maxDrop); _t->Put(r); _t->Put(a_outFloor); }
		++_n;
		return r;
	}
	bool RecordingWorld::Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac)
	{
		const bool r = _w ? _w->Cast(a_from, a_to, a_hitFrac) : false;
		if (_t) { _t->Put(Call::kCast); _t->Put(a_from); _t->Put(a_to); _t->Put(r); _t->Put(a_hitFrac); }
		++_n;
		return r;
	}
	int RecordingWorld::OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		const int r = _w ? _w->OpenDoors(a_from, a_to, a_radius) : -1;
		if (_t) { _t->Put(Call::kOpenDoors); _t->Put(a_from); _t->Put(a_to); _t->Put(a_radius); _t->Put(r); }
		++_n;
		return r;
	}
	int RecordingWorld::LockedDoorsSeen()
	{
		const int r = _w ? _w->LockedDoorsSeen() : 0;
		if (_t) { _t->Put(Call::kLockedDoors); _t->Put(r); }
		++_n;
		return r;
	}
	int RecordingWorld::SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		const int r = _w ? _w->SmashDoors(a_from, a_to, a_radius) : -1;
		if (_t) { _t->Put(Call::kSmashDoors); _t->Put(a_from); _t->Put(a_to); _t->Put(a_radius); _t->Put(r); }
		++_n;
		return r;
	}
	bool RecordingWorld::NavAvailable()
	{
		const bool r = _w ? _w->NavAvailable() : false;
		if (_t) { _t->Put(Call::kNavAvailable); _t->Put(r); }
		++_n;
		return r;
	}
	bool RecordingWorld::NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out)
	{
		const bool r = _w ? _w->NavPath(a_fromFeet, a_toFeet, a_targetId, a_agentR, a_out) : false;
		if (_t) {
			_t->Put(Call::kNavPath);
			_t->Put(a_fromFeet);
			_t->Put(a_toFeet);
			_t->Put(a_targetId);
			_t->Put(a_agentR);
			_t->Put(r);
			const std::uint32_t np = static_cast<std::uint32_t>(a_out.pts.size()), nf = static_cast<std::uint32_t>(a_out.flags.size());
			_t->Put(np);
			if (np) _t->PutRaw(a_out.pts.data(), sizeof(Vec3) * np);
			_t->Put(nf);
			if (nf) _t->PutRaw(a_out.flags.data(), nf);
			_t->Put(a_out.length);
			_t->Put(a_out.expanded);
			_t->Put(a_out.viaDoor);
			_t->Put(a_out.partial);
			_t->PutStr(a_out.why);
		}
		++_n;
		return r;
	}

	// ------------------------------------------------------------------------------------------------ TapeWorld
	bool TapeWorld::Expect(Call a_c)
	{
		Call c{};
		if (!_r.Get(c) || c != a_c) {
			++_miss;
			_r = Reader(nullptr, 0);  // diverged: every later call is a miss
			return false;
		}
		return true;
	}
	bool TapeWorld::ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps)
	{
		if (!Expect(Call::kClearTo)) return false;
		Vec3 f, t; std::uint32_t id = 0; double e = 0; bool r = false;
		_r.Get(f); _r.Get(t); _r.Get(id); _r.Get(e); _r.Get(r);
		if (!(f == a_from) || !(t == a_to) || id != a_targetId || e != a_reachEps) ++_miss;
		return r;
	}
	bool TapeWorld::Clear(const Vec3& a_from, const Vec3& a_to)
	{
		if (!Expect(Call::kClear)) return false;
		Vec3 f, t; bool r = false;
		_r.Get(f); _r.Get(t); _r.Get(r);
		if (!(f == a_from) || !(t == a_to)) ++_miss;
		return r;
	}
	bool TapeWorld::Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor)
	{
		if (!Expect(Call::kFloor)) return false;
		Vec3 f; double d = 0; bool r = false; Vec3 o;
		_r.Get(f); _r.Get(d); _r.Get(r); _r.Get(o);
		if (!(f == a_from) || d != a_maxDrop) ++_miss;
		a_outFloor = o;
		return r;
	}
	bool TapeWorld::Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac)
	{
		if (!Expect(Call::kCast)) { a_hitFrac = 0; return true; }
		Vec3 f, t; bool r = false; double h = -1;
		_r.Get(f); _r.Get(t); _r.Get(r); _r.Get(h);
		if (!(f == a_from) || !(t == a_to)) ++_miss;
		a_hitFrac = h;
		return r;
	}
	int TapeWorld::OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		if (!Expect(Call::kOpenDoors)) return -1;
		Vec3 f, t; double rad = 0; int r = -1;
		_r.Get(f); _r.Get(t); _r.Get(rad); _r.Get(r);
		if (!(f == a_from) || !(t == a_to) || rad != a_radius) ++_miss;
		return r;
	}
	int TapeWorld::LockedDoorsSeen()
	{
		if (!Expect(Call::kLockedDoors)) return 0;
		int r = 0;
		_r.Get(r);
		return r;
	}
	int TapeWorld::SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius)
	{
		if (!Expect(Call::kSmashDoors)) return -1;
		Vec3 f, t; double rad = 0; int r = -1;
		_r.Get(f); _r.Get(t); _r.Get(rad); _r.Get(r);
		if (!(f == a_from) || !(t == a_to) || rad != a_radius) ++_miss;
		return r;
	}
	bool TapeWorld::NavAvailable()
	{
		if (!Expect(Call::kNavAvailable)) return false;
		bool r = false;
		_r.Get(r);
		return r;
	}
	bool TapeWorld::NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out)
	{
		a_out = {};
		if (!Expect(Call::kNavPath)) { a_out.why = "tape"; return false; }
		Vec3 f, t; std::uint32_t id = 0; double ar = 0; bool r = false;
		_r.Get(f); _r.Get(t); _r.Get(id); _r.Get(ar); _r.Get(r);
		if (!(f == a_fromFeet) || !(t == a_toFeet) || id != a_targetId || ar != a_agentR) ++_miss;
		std::uint32_t np = 0, nf = 0;
		if (_r.Get(np) && np < 100000) {
			a_out.pts.resize(np);
			if (np) _r.GetRaw(a_out.pts.data(), sizeof(Vec3) * np);
		}
		if (_r.Get(nf) && nf < 100000) {
			a_out.flags.resize(nf);
			if (nf) _r.GetRaw(a_out.flags.data(), nf);
		}
		_r.Get(a_out.length);
		_r.Get(a_out.expanded);
		_r.Get(a_out.viaDoor);
		_r.Get(a_out.partial);
		std::string why;
		_r.GetStr(why);
		_strs.push_back(std::move(why));
		if (_strs.size() > 4096) _strs.pop_front();
		a_out.why = _strs.back().c_str();
		return r;
	}

	// ------------------------------------------------------------------------------------------------ Recorder
	void Recorder::StartSegment(const Brain& a_brain, const Params& a_p, double a_t)
	{
		const std::size_t keep = static_cast<std::size_t>(std::clamp(_cfg.segments, 2, kMaxSegments));
		while (_segs.size() >= keep) _segs.erase(_segs.begin());
		Segment s;
		s.key = std::make_shared<const Brain>(a_brain);
		s.wkey = std::move(_wPendingKey);  // R6 step 4: the Warden before this frame's Update (null: it did not pick)
		_wPendingKey.reset();
		s.params = a_p;
		s.t0 = s.t1 = a_t;
		s.fromReset = _resetPending;
		if (_resetPending) s.preExt = _ext.v;  // the calls since the Reset (in the keyframe; a fresh-brain replay needs them)
		_segs.push_back(std::move(s));
		++_segsTotal;
		_resetPending = false;
		_ext.v.clear();  // already inside the keyframe state
		_paramsChanged = false;
		_paramsHash = HashParams(a_p);
		_hashedP = a_p;
		_haveHashedP = true;
	}

	bool Recorder::NeedNew(double now) const
	{
		const int         nSeg = std::clamp(_cfg.segments, 2, kMaxSegments);
		const std::size_t segCap = static_cast<std::size_t>(std::min(std::max(0.05, std::max(_cfg.memMB, _cfg.maxMB)) / nSeg, std::max(0.05, _cfg.maxMB) * 0.5) * 1048576.0);
		return _segs.empty() || now < _segs.back().t0 || now - _segs.back().t0 >= std::max(0.5, _cfg.seconds / nSeg) || _segs.back().packed.size() >= segCap;
	}

	IWorld& Recorder::Begin(const Brain& a_brain, const Facts& a_f, const Params& a_p, IWorld& a_inner, double a_wall)
	{
		if (!_cfg.on) return a_inner;
		const auto t0 = std::chrono::steady_clock::now();
		const double now = a_f.now;
		const bool   needNew = _forceNew || NeedNew(now);  // R6 step 4: WardenBegin may have decided it (same clock)
		_forceNew = false;
		if (needNew) StartSegment(a_brain, a_p, now);
		// step-2 fixer (COST): hash only when the params bytes changed since the last hash (same result, ~25x cheaper)
		std::uint64_t ph = _paramsHash;
		if (!_haveHashedP || std::memcmp(&a_p, &_hashedP, sizeof(Params)) != 0) {
			ph = HashParams(a_p);
			_hashedP = a_p;
			_haveHashedP = true;
		}
		if (ph != _paramsHash) {
			_ext.Put(kExtParams);
			_ext.Put(a_p);
			_paramsHash = ph;
		}
		_f = a_f;
		_f.cands = {};
		_cands.assign(a_f.cands.begin(), a_f.cands.end());
		_wall = a_wall;
		_tape.v.clear();
		_world.Begin(&a_inner, &_tape);
		_inFrame = true;
		_lastUs = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		return _world;
	}

	void Recorder::End(const Out& a_out)
	{
		if (!_cfg.on || !_inFrame || _segs.empty()) return;
		const auto t0 = std::chrono::steady_clock::now();
		_inFrame = false;
		Segment& s = _segs.back();
		_rec.v.clear();
		_rec.Put(kFrameMagic);
		_rec.Put(_wall);
		_rec.PutRaw(&_f, sizeof(Facts));
		const std::uint32_t nc = static_cast<std::uint32_t>(_cands.size());
		_rec.Put(nc);
		if (nc) _rec.PutRaw(_cands.data(), sizeof(Cand) * nc);
		_rec.Put(_paramsHash);
		const std::uint32_t ne = static_cast<std::uint32_t>(_ext.v.size());
		_rec.Put(ne);
		if (ne) _rec.PutRaw(_ext.v.data(), ne);
		const std::uint32_t nt = static_cast<std::uint32_t>(_tape.v.size());
		_rec.Put(nt);
		if (nt) _rec.PutRaw(_tape.v.data(), nt);
		_rec.Put(KeyOf(a_out));
		{  // R6 step 4: the Warden's record of this frame
			const std::uint32_t nw = _wHave ? static_cast<std::uint32_t>(_wrec.v.size()) : 0u;
			_rec.Put(nw);
			if (nw) _rec.PutRaw(_wrec.v.data(), nw);
			_wHave = false;
		}
		_ext.v.clear();
		_scratch.assign(_rec.v.begin(), _rec.v.end());
		XorInto(_scratch, s.last);
		Pack(_scratch, s.packed);
		s.last.swap(_rec.v);
		s.t1 = _f.now;
		++s.frames;
		++_framesTotal;
		_lastUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
		_sumUs += _lastUs;  // integrator: the mean record cost per brain frame (the step-2 cost gate: <= 20 us)
		++_nUs;
	}

	void Recorder::NoteReset()
	{
		_ext.v.clear();  // a fresh brain = a Reset brain (the next keyframe); the calls after it are kept as the preExt
		_resetPending = true;
		_segs.clear();  // the next frame starts a fresh, replayable-from-Reset segment
	}
	void Recorder::NoteOnLoad()
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kOnLoad);
	}
	void Recorder::NoteOnTeleport()
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kOnTeleport);
	}
	void Recorder::NoteOnOwnBlink(std::uint32_t a_target)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kOnOwnBlink);
		_ext.Put(a_target);
	}
	void Recorder::NoteHit(std::uint32_t a_id, double a_now, double a_dist)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kNoteHit);
		_ext.Put(a_id);
		_ext.Put(a_now);
		_ext.Put(a_dist);
	}
	void Recorder::NoteIgnore(std::uint32_t a_id, double a_now, double a_forS)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kIgnore);
		_ext.Put(a_id);
		_ext.Put(a_now);
		_ext.Put(a_forS);
	}
	void Recorder::NoteRecall(bool a_fly, double a_now)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kRecall);
		_ext.Put(a_fly);
		_ext.Put(a_now);
	}
	void Recorder::NoteSetVerdicts(std::span<const std::uint32_t> a_deny, std::span<const std::uint32_t> a_clear, double a_now, double a_hp)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kSetVerdicts);
		const std::uint32_t nd = static_cast<std::uint32_t>(a_deny.size()), ncl = static_cast<std::uint32_t>(a_clear.size());
		_ext.Put(nd);
		if (nd) _ext.PutRaw(a_deny.data(), 4 * nd);
		_ext.Put(ncl);
		if (ncl) _ext.PutRaw(a_clear.data(), 4 * ncl);
		_ext.Put(a_now);
		_ext.Put(a_hp);
	}
	void Recorder::NoteSpared(std::uint32_t a_id, double a_now)
	{
		if (!_cfg.on) return;
		_ext.Put(Ext::kNoteSpared);
		_ext.Put(a_id);
		_ext.Put(a_now);
	}

	// ------------------------------------------------------------------------------------------------ R6 step 4: the Warden
	namespace
	{
		constexpr std::uint32_t kWMagic = 0x31445257u;  // "WRD1"
		static_assert(std::is_trivially_copyable_v<sp::warden::Cand>, "warden::Cand is recorded raw");
		static_assert(std::is_trivially_copyable_v<sp::warden::ReachEst>, "warden::ReachEst is recorded raw");
		static_assert(std::is_trivially_copyable_v<sp::warden::Params>, "warden::Params is recorded raw");
		static_assert(std::is_trivially_copyable_v<sp::warden::Me> && std::is_trivially_copyable_v<sp::warden::PlayerF> && std::is_trivially_copyable_v<sp::warden::Gates>);
		static_assert(std::is_copy_constructible_v<sp::warden::Warden>, "the Warden keyframe is a copy");
	}
	std::uint64_t WardenKeyHash(const sp::warden::Decision& a_d)
	{
		const std::string k = sp::warden::DecisionKey(a_d);
		return Fnv(k.data(), k.size());
	}
	void Recorder::WardenBegin(const sp::warden::Warden& a_before, double a_now)
	{
		if (!_cfg.on) return;
		if (NeedNew(a_now)) {
			// the next brain record starts a segment: this Warden (before its Update) is its keyframe; the calls noted so far
			// are inside that state already
			_forceNew = true;
			_wPendingKey = std::make_shared<const sp::warden::Warden>(a_before);
			_wext.v.clear();
		}
	}
	void Recorder::WardenEnd(const sp::warden::WFacts& a_f, const sp::warden::Params& a_p, const sp::warden::Decision& a_d)
	{
		if (!_cfg.on) return;
		_wrec.v.clear();
		_wrec.Put(kWMagic);
		const std::uint32_t ne = static_cast<std::uint32_t>(_wext.v.size());
		_wrec.Put(ne);
		if (ne) _wrec.PutRaw(_wext.v.data(), ne);
		_wrec.Put(a_f.now);
		_wrec.Put(a_f.dt);
		_wrec.Put(a_f.wall);
		_wrec.Put(a_f.me);
		_wrec.Put(a_f.player);
		_wrec.Put(a_f.gates);
		const std::uint32_t nc = static_cast<std::uint32_t>(a_f.cands.size());
		_wrec.Put(nc);
		if (nc) _wrec.PutRaw(a_f.cands.data(), sizeof(sp::warden::Cand) * nc);
		const std::uint32_t nr = static_cast<std::uint32_t>(a_f.reach.size());
		_wrec.Put(nr);
		if (nr) _wrec.PutRaw(a_f.reach.data(), sizeof(sp::warden::ReachEst) * nr);
		_wrec.Put(a_p);
		_wrec.Put(WardenKeyHash(a_d));
		_wext.v.clear();
		_wHave = true;
	}
	void Recorder::WardenNoteHit(std::uint32_t a_id, double a_now)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kNoteHit);
		_wext.Put(a_id);
		_wext.Put(a_now);
	}
	void Recorder::WardenNoteKill(std::uint32_t a_id)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kNoteKill);
		_wext.Put(a_id);
	}
	void Recorder::WardenNoteSpared(std::uint32_t a_id, double a_wall)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kNoteSpared);
		_wext.Put(a_id);
		_wext.Put(a_wall);
	}
	void Recorder::WardenNoteReachFail(std::uint32_t a_id, double a_now, const char* a_why)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kNoteReachFail);
		_wext.Put(a_id);
		_wext.Put(a_now);
		_wext.PutStr(a_why);
	}
	void Recorder::WardenNoteDefendJump(double a_wall)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kNoteDefendJump);
		_wext.Put(a_wall);
	}
	void Recorder::WardenRecall(bool a_fly, double a_now)
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kRecall);
		_wext.Put(a_fly);
		_wext.Put(a_now);
	}
	void Recorder::WardenOnJump()
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kOnJump);
	}
	void Recorder::WardenOnLoad()
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kOnLoad);
	}
	void Recorder::WardenReset()
	{
		if (!_cfg.on) return;
		_wext.Put(WExt::kReset);
	}

	bool DecodeWarden(const std::vector<std::uint8_t>& a_wrec, WFrame& a_out)
	{
		a_out = WFrame{};
		Reader        r(a_wrec.data(), a_wrec.size());
		std::uint32_t magic = 0, ne = 0, nc = 0, nr = 0;
		if (!r.Get(magic) || magic != kWMagic) return false;
		if (!r.Get(ne) || ne > r.Left()) return false;
		a_out.ext.resize(ne);
		if (ne) r.GetRaw(a_out.ext.data(), ne);
		r.Get(a_out.facts.now);
		r.Get(a_out.facts.dt);
		r.Get(a_out.facts.wall);
		r.Get(a_out.facts.me);
		r.Get(a_out.facts.player);
		r.Get(a_out.facts.gates);
		if (!r.Get(nc) || nc > 4096) return false;
		a_out.cands.resize(nc);
		if (nc) r.GetRaw(a_out.cands.data(), sizeof(sp::warden::Cand) * nc);
		if (!r.Get(nr) || nr > 4096) return false;
		a_out.reach.resize(nr);
		if (nr) r.GetRaw(a_out.reach.data(), sizeof(sp::warden::ReachEst) * nr);
		r.Get(a_out.params);
		r.Get(a_out.key);
		a_out.facts.cands = std::span<const sp::warden::Cand>(a_out.cands.data(), a_out.cands.size());
		a_out.facts.reach = std::span<const sp::warden::ReachEst>(a_out.reach.data(), a_out.reach.size());
		return r.Ok();
	}

	namespace
	{
		void ApplyWExt(sp::warden::Warden& w, const std::vector<std::uint8_t>& a_ext)
		{
			Reader r(a_ext.data(), a_ext.size());
			while (r.Left() > 0 && r.Ok()) {
				std::uint8_t k = 0;
				if (!r.Get(k)) break;
				switch (static_cast<WExt>(k)) {
				case WExt::kReset: w.Reset(); break;
				case WExt::kOnLoad: w.OnLoad(); break;
				case WExt::kOnJump: w.OnJump(); break;
				case WExt::kNoteHit: { std::uint32_t id = 0; double n = 0; r.Get(id); r.Get(n); w.NoteHit(id, n); break; }
				case WExt::kNoteKill: { std::uint32_t id = 0; r.Get(id); w.NoteKill(id); break; }
				case WExt::kNoteSpared: { std::uint32_t id = 0; double wl = 0; r.Get(id); r.Get(wl); (void)w.NoteSpared(id, wl); break; }
				case WExt::kNoteReachFail: { std::uint32_t id = 0; double n = 0; std::string why; r.Get(id); r.Get(n); r.GetStr(why); w.NoteReachFail(id, n, why.c_str()); break; }
				case WExt::kNoteDefendJump: { double wl = 0; r.Get(wl); w.NoteDefendJump(wl); break; }
				case WExt::kRecall: { bool fly = false; double n = 0; r.Get(fly); r.Get(n); w.Recall(fly, n); break; }
				default: return;  // unknown: stop (corrupt)
				}
			}
		}
	}

	ReplayResult ReplayWarden(sp::warden::Warden& a_w, std::span<const Frame> a_frames)
	{
		ReplayResult res;
		WFrame       wf;
		for (std::size_t i = 0; i < a_frames.size(); ++i) {
			const Frame& f = a_frames[i];
			if (f.wrec.empty()) continue;
			if (!DecodeWarden(f.wrec, wf)) {
				++res.wMismatches;
				if (res.wFirstBad < 0) res.wFirstBad = static_cast<std::int64_t>(i);
				continue;
			}
			ApplyWExt(a_w, wf.ext);
			sp::warden::Decision d;
			a_w.Update(wf.facts, wf.params, d);
			a_w.Lines().clear();  // (the glue drains them every frame)
			++res.wFrames;
			if (WardenKeyHash(d) != wf.key) {
				++res.wMismatches;
				if (res.wFirstBad < 0) res.wFirstBad = static_cast<std::int64_t>(i);
			}
		}
		return res;
	}

	Snapshot Recorder::TakeSnapshot(const char* a_why, double a_wall) const
	{
		Snapshot s;
		s.segs = _segs;  // shared keyframes, copied byte vectors
		for (auto& g : s.segs) g.last.clear();
		s.why = a_why ? a_why : "";
		s.wallAt = a_wall;
		s.brainAt = _f.now;
		return s;
	}

	void Recorder::Clear()
	{
		_segs.clear();
		_ext.v.clear();
		_tape.v.clear();
		_inFrame = false;
		_resetPending = true;
		_wext.v.clear();  // R6 step 4
		_wrec.v.clear();
		_wHave = _forceNew = false;
		_wPendingKey.reset();
	}

	std::size_t Recorder::PackedBytes() const
	{
		std::size_t n = 0;
		for (const auto& s : _segs) n += s.packed.size();
		return n;
	}

	// ------------------------------------------------------------------------------------------------ decode / replay
	bool Decode(const Segment& a_seg, std::vector<Frame>& a_out)
	{
		a_out.clear();
		Reader                    r(a_seg.packed.data(), a_seg.packed.size());
		std::vector<std::uint8_t> base, x;
		while (r.Left() > 0) {
			if (!Unpack(r, x)) return false;
			XorInto(x, base);
			Frame f;
			if (!DecodeRecord(x, f)) return false;
			a_out.push_back(std::move(f));
			base.swap(x);
		}
		// facts.cands must point into the stored vectors (moved: re-point)
		for (auto& f : a_out) f.facts.cands = std::span<const Cand>(f.cands.data(), f.cands.size());
		return r.Ok();
	}

	namespace
	{
		void ApplyExt(Brain& b, Params& a_params, const std::vector<std::uint8_t>& a_ext)
		{
			Reader r(a_ext.data(), a_ext.size());
			while (r.Left() > 0 && r.Ok()) {
				std::uint8_t k = 0;
				if (!r.Get(k)) break;
				switch (k) {
				case static_cast<std::uint8_t>(Ext::kReset): b.Reset(); break;
				case static_cast<std::uint8_t>(Ext::kOnLoad): b.OnLoad(); break;
				case static_cast<std::uint8_t>(Ext::kOnTeleport): b.OnTeleport(); break;
				case static_cast<std::uint8_t>(Ext::kOnOwnBlink): { std::uint32_t t = 0; r.Get(t); b.OnOwnBlink(t); break; }
				case static_cast<std::uint8_t>(Ext::kNoteHit): { std::uint32_t id = 0; double n = 0, d = 0; r.Get(id); r.Get(n); r.Get(d); b.NoteHit(id, n, d); break; }
				case static_cast<std::uint8_t>(Ext::kIgnore): { std::uint32_t id = 0; double n = 0, s = 0; r.Get(id); r.Get(n); r.Get(s); b.Ignore(id, n, s); break; }
				case static_cast<std::uint8_t>(Ext::kRecall): { bool fly = false; double n = 0; r.Get(fly); r.Get(n); b.Recall(fly, n); break; }
				case static_cast<std::uint8_t>(Ext::kSetVerdicts): {
					std::uint32_t nd = 0, nc = 0;
					std::vector<std::uint32_t> d, c;
					double n = 0, hp = -1;
					if (r.Get(nd) && nd < 100000) { d.resize(nd); if (nd) r.GetRaw(d.data(), 4 * nd); }
					if (r.Get(nc) && nc < 100000) { c.resize(nc); if (nc) r.GetRaw(c.data(), 4 * nc); }
					r.Get(n);
					r.Get(hp);
					b.SetVerdicts(d, c, n, hp);
					break;
				}
				case static_cast<std::uint8_t>(Ext::kNoteSpared): { std::uint32_t id = 0; double n = 0; r.Get(id); r.Get(n); (void)b.NoteSpared(id, n); break; }
				case kExtParams: r.Get(a_params); break;
				default: return;  // unknown: stop (corrupt)
				}
			}
		}
	}

	ReplayResult Replay(Brain& a_brain, Params a_params, std::span<const Frame> a_frames)
	{
		ReplayResult res;
		TapeWorld    w;
		for (std::size_t i = 0; i < a_frames.size(); ++i) {
			const Frame& f = a_frames[i];
			ApplyExt(a_brain, a_params, f.ext);
			w.Begin(f.tape.data(), f.tape.size());
			const int missBefore = w.Misses();
			Out o;
			a_brain.Update(f.facts, a_params, w, o);
			const bool tapeOk = w.Misses() == missBefore && w.Exhausted();
			if (!tapeOk) ++res.tapeMisses;
			const bool same = KeyOf(o) == f.dec;
			if (!same) ++res.mismatches;
			if ((!same || !tapeOk) && res.firstBad < 0) res.firstBad = static_cast<std::int64_t>(i);
			++res.frames;
		}
		return res;
	}

	ReplayResult ReplaySnapshot(const Snapshot& a_s)
	{
		ReplayResult all;
		for (const auto& seg : a_s.segs) {
			if (!seg.key || seg.frames == 0) continue;
			std::vector<Frame> fr;
			if (!Decode(seg, fr)) {
				++all.mismatches;
				continue;
			}
			Brain b = *seg.key;
			const auto r = Replay(b, seg.params, fr);
			if (r.firstBad >= 0 && all.firstBad < 0) all.firstBad = static_cast<std::int64_t>(all.frames) + r.firstBad;
			// R6 step 4: the Warden's frames from the segment's Warden keyframe
			if (seg.wkey) {
				sp::warden::Warden w = *seg.wkey;
				const auto         rw = ReplayWarden(w, fr);
				if (rw.wFirstBad >= 0 && all.wFirstBad < 0) all.wFirstBad = static_cast<std::int64_t>(all.frames) + rw.wFirstBad;
				all.wFrames += rw.wFrames;
				all.wMismatches += rw.wMismatches;
			} else {
				for (const auto& f : fr)
					if (!f.wrec.empty()) ++all.wNoKey;
			}
			all.frames += r.frames;
			all.mismatches += r.mismatches;
			all.tapeMisses += r.tapeMisses;
		}
		return all;
	}

	// ------------------------------------------------------------------------------------------------ file
	bool Serialize(const Snapshot& a_s, std::vector<std::uint8_t>& a_out, std::size_t a_maxBytes, const FileInfo* a_extra)
	{
		a_out.clear();
		if (a_s.Empty()) return false;
		// which segments fit (newest first)
		std::size_t first = 0;
		{
			std::size_t total = 512;
			for (std::size_t i = a_s.segs.size(); i-- > 0;) {
				const std::size_t need = a_s.segs[i].packed.size() + a_s.segs[i].preExt.size() + sizeof(Params) + 64 + (a_s.segs[i].wkey ? 16384u : 0u);  // R6 step 4: + the Warden keyframe
				if (total + need > a_maxBytes && i + 1 < a_s.segs.size()) {
					first = i + 1;
					break;
				}
				total += need;
			}
		}
		sp::bb::Bytes b;
		b.Put(kFileMagic);
		b.Put(kVersion);
		const std::uint32_t sizes[5] = { static_cast<std::uint32_t>(sizeof(Facts)), static_cast<std::uint32_t>(sizeof(Cand)), static_cast<std::uint32_t>(sizeof(Params)),
			static_cast<std::uint32_t>(sizeof(DecisionKey)), static_cast<std::uint32_t>(sizeof(Vec3)) };
		b.PutRaw(sizes, sizeof(sizes));
		b.PutStr(a_s.why.c_str());
		b.Put(a_s.wallAt);
		b.Put(a_s.brainAt);
		b.Put(a_s.seq);
		const std::uint32_t nseg = static_cast<std::uint32_t>(a_s.segs.size() - first);
		b.Put(nseg);
		const bool fromReset = a_s.segs[first].fromReset;
		b.Put(fromReset);
		const std::int32_t  igr = a_extra ? a_extra->inGameReplay : -1;
		const std::uint32_t igf = a_extra ? a_extra->inGameFrames : 0, igm = a_extra ? a_extra->inGameMismatch : 0;
		b.Put(igr);
		b.Put(igf);
		b.Put(igm);
		for (std::size_t i = first; i < a_s.segs.size(); ++i) {
			const auto& s = a_s.segs[i];
			b.Put(s.params);
			b.Put(s.t0);
			b.Put(s.t1);
			b.Put(s.frames);
			b.Put(s.fromReset);
			const std::uint32_t npe = static_cast<std::uint32_t>(s.preExt.size());
			b.Put(npe);
			if (npe) b.PutRaw(s.preExt.data(), npe);
			const std::uint64_t n = s.packed.size();
			b.Put(n);
			b.PutRaw(s.packed.data(), s.packed.size());
			// R6 step 4 (v2): the Warden keyframe of the segment (SaveState bytes; 0 = none)
			const std::vector<std::uint8_t> wk = s.wkey ? s.wkey->SaveState() : s.wkeyBytes;
			const std::uint32_t             nwk = static_cast<std::uint32_t>(wk.size());
			b.Put(nwk);
			if (nwk) b.PutRaw(wk.data(), nwk);
		}
		a_out.swap(b.v);
		return true;
	}

	bool Parse(const std::uint8_t* a_p, std::size_t a_n, Parsed& a_out, std::string* a_err)
	{
		a_out = {};
		auto fail = [&](const char* w) { if (a_err) *a_err = w; return false; };
		Reader r(a_p, a_n);
		std::uint32_t magic = 0, ver = 0;
		if (!r.Get(magic) || magic != kFileMagic) return fail("magic");
		if (!r.Get(ver) || ver != kVersion) return fail("version");
		std::uint32_t sizes[5]{};
		if (!r.GetRaw(sizes, sizeof(sizes))) return fail("sizes");
		if (sizes[0] != sizeof(Facts) || sizes[1] != sizeof(Cand) || sizes[2] != sizeof(Params) || sizes[3] != sizeof(DecisionKey) || sizes[4] != sizeof(Vec3))
			return fail("layout (another build)");
		auto& in = a_out.info;
		in.version = ver;
		r.GetStr(in.why);
		r.Get(in.wallAt);
		r.Get(in.brainAt);
		r.Get(in.seq);
		std::uint32_t nseg = 0;
		if (!r.Get(nseg) || nseg > static_cast<std::uint32_t>(kMaxSegments)) return fail("segments");
		r.Get(in.replayableFromReset);
		r.Get(in.inGameReplay);
		r.Get(in.inGameFrames);
		r.Get(in.inGameMismatch);
		in.segs = nseg;
		for (std::uint32_t i = 0; i < nseg; ++i) {
			Segment s;
			r.Get(s.params);
			r.Get(s.t0);
			r.Get(s.t1);
			r.Get(s.frames);
			r.Get(s.fromReset);
			std::uint32_t npe = 0;
			if (!r.Get(npe) || npe > r.Left()) return fail("preExt");
			s.preExt.resize(npe);
			if (npe) r.GetRaw(s.preExt.data(), npe);
			std::uint64_t n = 0;
			if (!r.Get(n) || n > r.Left()) return fail("segment bytes");
			s.packed.resize(static_cast<std::size_t>(n));
			if (n) r.GetRaw(s.packed.data(), static_cast<std::size_t>(n));
			std::uint32_t nwk = 0;  // R6 step 4 (v2)
			if (!r.Get(nwk) || nwk > r.Left()) return fail("warden keyframe");
			s.wkeyBytes.resize(nwk);
			if (nwk) r.GetRaw(s.wkeyBytes.data(), nwk);
			std::vector<Frame> fr;
			if (!Decode(s, fr)) return fail("frames");
			in.frames += static_cast<std::uint32_t>(fr.size());
			for (const auto& f : fr)
				if (!f.wrec.empty()) ++in.wFrames;
			a_out.segParams.push_back(s.params);
			a_out.segFrames.push_back(std::move(fr));
			a_out.segPreExt.push_back(std::move(s.preExt));
			a_out.segWKey.push_back(std::move(s.wkeyBytes));
		}
		return r.Ok() ? true : fail("truncated");
	}

	ReplayResult ReplayFile(const Parsed& a_p)
	{
		ReplayResult all;
		if (a_p.segFrames.empty()) return all;
		// one continuous run over every segment from a fresh brain (frames of later segments follow on)
		Brain  b;
		Params pr = a_p.segParams.front();
		if (!a_p.segPreExt.empty()) ApplyExt(b, pr, a_p.segPreExt.front());  // the calls between the Reset and the first frame
		// R6 step 4: the Warden from the first segment's Warden keyframe (the same build's SaveState bytes), one continuous run
		sp::warden::Warden w;
		const bool         haveW = !a_p.segWKey.empty() && !a_p.segWKey.front().empty() && w.LoadState(a_p.segWKey.front().data(), a_p.segWKey.front().size());
		for (std::size_t i = 0; i < a_p.segFrames.size(); ++i) {
			const auto r = Replay(b, pr, a_p.segFrames[i]);
			if (r.firstBad >= 0 && all.firstBad < 0) all.firstBad = static_cast<std::int64_t>(all.frames) + r.firstBad;
			if (haveW) {
				const auto rw = ReplayWarden(w, a_p.segFrames[i]);
				if (rw.wFirstBad >= 0 && all.wFirstBad < 0) all.wFirstBad = static_cast<std::int64_t>(all.frames) + rw.wFirstBad;
				all.wFrames += rw.wFrames;
				all.wMismatches += rw.wMismatches;
			} else {
				for (const auto& f : a_p.segFrames[i])
					if (!f.wrec.empty()) ++all.wNoKey;
			}
			all.frames += r.frames;
			all.mismatches += r.mismatches;
			all.tapeMisses += r.tapeMisses;
			if (i + 1 < a_p.segParams.size()) pr = a_p.segParams[i + 1];
		}
		return all;
	}

	std::string Summary(const Parsed& a_p, std::size_t a_maxFrames)
	{
		const auto& in = a_p.info;
		std::string s = std::format("black box v{} why={} wallAt={:.3f} brainAt={:.3f} seq={} segments={} frames={} replayableFromReset={} inGameReplay={} ({} frames, {} mismatched) wardenFrames={}\n",
			in.version, in.why, in.wallAt, in.brainAt, in.seq, in.segs, in.frames, in.replayableFromReset, in.inGameReplay, in.inGameFrames, in.inGameMismatch, in.wFrames);
		s += "t,target,tier,move,hunt,vis,may,dist,cands,player_hp,blink,putBack,giveUp\n";
		std::vector<const Frame*> all;
		for (const auto& fr : a_p.segFrames)
			for (const auto& f : fr) all.push_back(&f);
		const std::size_t from = all.size() > a_maxFrames ? all.size() - a_maxFrames : 0;
		std::uint32_t     lastTarget = 0xFFFFFFFFu;
		for (std::size_t i = 0; i < all.size(); ++i) {
			const Frame& f = *all[i];
			const bool   change = f.dec.target != lastTarget;
			lastTarget = f.dec.target;
			if (i < from && !change && !(f.dec.flags & ((1u << 3) | (1u << 4)))) continue;
			s += std::format("{:.3f},{:08X},{},{},{},{},{},{:.0f},{},{:.2f},{},{},{}\n", f.facts.now, f.dec.target, f.dec.tier, f.dec.move, f.dec.hunt, (f.dec.flags & 1) ? 1 : 0,
				(f.dec.flags & 2) ? 1 : 0, f.dec.dist, f.cands.size(), f.facts.playerHealthPct, (f.dec.flags & 8) ? 1 : 0, (f.dec.flags & 16) ? 1 : 0, f.dec.giveUp);
		}
		return s;
	}
}
