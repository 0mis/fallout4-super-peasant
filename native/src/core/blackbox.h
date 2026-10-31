#pragma once
// OWNER: R6 (round 6 step 2, ARCH S0 §2.7 "black box"). Pure C++ (no RE / F4SE headers; compiled into sp_tests).
//
// The decision recorder of the D2f brain (the Warden replaces the brain from step 3 on and records the same way):
//   - every brain frame (every Brain::Update, not sampled: replay needs them all) is one record: the Facts, the kept
//     candidates, the Params hash (the Params themselves when they change), the brain's external calls since the last
//     frame (Reset, OnLoad, OnTeleport, OnOwnBlink, NoteHit, Ignore, Recall, SetVerdicts, NoteSpared), every IWorld call
//     the brain made with its answer (the "tape") and the decision (DecisionKey);
//   - records are XOR-delta coded against the previous record and zero-run packed (a quiet frame packs to ~100 bytes);
//   - bb.segments (6) segments; a segment closes after bb.seconds / segments s or bb.memMB / segments packed bytes,
//     starts with an in-memory copy of the Brain (the keyframe), and the newest `segments` are kept: the ring holds
//     (segments - 1) .. segments full segments = 25..30 s in normal fights, >= 10 s at 144 fps with 24 moving foes;
//     the file keeps the newest segments that fit bb.maxMB (2);
//   - Snapshot() at the death edge (player health 0 / dead), on [Debug] BlackBoxDump, on the bookmark key: copies the
//     segments + the keyframe (one memcpy on the game thread); the writer (fo4 side, low-priority thread) replays it from
//     the keyframe (proves "replay = identical decisions" in game: SPN|BB|dump ... replay=identical n=) and writes
//     <F4SE>\SuperPeasantNative.bb<seq> (<= bb.maxMB, the last bb.maxDumps kept) + a .txt summary;
//   - Serialize / Parse / Replay: the file format, and a replay from a given starting Brain (tests: a recording that
//     starts at Brain::Reset replays bit for bit from a fresh, Reset Brain; the plugin: from the keyframe copy).
// The file layout is the recording build's struct layout (ARCH R12: cross-build replays compare decisions only).

#include "core/brain.h"
#include "core/warden.h"  // R6 step 4: the Warden's frames ride in the same records ([R6] Warden 2)

#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace sp::bb
{
	using sp::brain::Brain;
	using sp::brain::Cand;
	using sp::brain::Facts;
	using sp::brain::IWorld;
	using sp::brain::Out;
	using sp::brain::Params;
	using sp::math::Vec3;

	constexpr std::uint32_t kFileMagic = 0x42425053u;  // "SPBB"
	constexpr std::uint32_t kVersion = 2;              // R6 step 4: + the Warden's frames and its keyframe per segment
	constexpr int           kMaxSegments = 32;  // bb.segments is clamped to 2..kMaxSegments; Parse accepts the same limit (F6)

	// The decision compared bit for bit by Replay (no pointers: Out's why strings are logs).
	struct DecisionKey
	{
		std::uint32_t target{ 0 };
		std::int32_t  tier{ 0 }, cls{ 0 }, move{ 0 }, hunt{ 0 }, blinkWhy{ 0 }, putBackWhy{ 0 }, giveUp{ 0 }, reachStage{ -1 }, planKind{ 0 }, rays{ 0 };
		std::uint32_t giveUpId{ 0 }, huntEndId{ 0 };
		std::uint32_t flags{ 0 };  // bit 0 visible, 1 mayAttack, 2 pin, 3 blink, 4 putBack, 5 huntStart, 6 huntAttack, 7 huntEnd, 8 returnStart,
		                          //     9 chaseRequest, 10 vetRequest, 11 targetChanged, 12 climb, 13 launch, 14 directClear, 15 blinkAttack,
		                          //     16 reachFail (R6 step 4)
		double        dist{ 0 }, dvMax{ 0 }, vMax{ 0 };
		Vec3          aim{}, vDes{}, face{}, blinkFeet{};
		bool operator==(const DecisionKey&) const = default;
	};
	DecisionKey KeyOf(const Out& a_out);
	std::uint64_t HashOf(const DecisionKey& a_k);

	enum class Ext : std::uint8_t
	{
		kReset = 1, kOnLoad = 2, kOnTeleport = 3, kOnOwnBlink = 4, kNoteHit = 5, kIgnore = 6, kRecall = 7, kSetVerdicts = 8, kNoteSpared = 9
	};
	enum class Call : std::uint8_t
	{
		kClearTo = 1, kClear = 2, kFloor = 3, kCast = 4, kOpenDoors = 5, kLockedDoors = 6, kSmashDoors = 7, kNavAvailable = 8, kNavPath = 9
	};

	// Little byte writer / reader used by the tape and the file.
	class Bytes
	{
	public:
		std::vector<std::uint8_t> v;
		template <class T>
		void Put(const T& a)
		{
			const auto* p = reinterpret_cast<const std::uint8_t*>(&a);
			v.insert(v.end(), p, p + sizeof(T));
		}
		void PutRaw(const void* a_p, std::size_t a_n)
		{
			const auto* p = static_cast<const std::uint8_t*>(a_p);
			v.insert(v.end(), p, p + a_n);
		}
		void PutStr(const char* a_s);
	};
	class Reader
	{
	public:
		Reader(const std::uint8_t* a_p, std::size_t a_n) : _p(a_p), _n(a_n) {}
		template <class T>
		bool Get(T& a)
		{
			if (_i + sizeof(T) > _n) return Fail();
			std::memcpy(&a, _p + _i, sizeof(T));
			_i += sizeof(T);
			return true;
		}
		bool GetRaw(void* a_out, std::size_t a_n)
		{
			if (_i + a_n > _n) return Fail();
			std::memcpy(a_out, _p + _i, a_n);
			_i += a_n;
			return true;
		}
		bool GetStr(std::string& a_out);
		bool        Ok() const { return _ok; }
		std::size_t Left() const { return _n - _i; }
		std::size_t Pos() const { return _i; }

	private:
		bool                Fail() { _ok = false; return false; }
		const std::uint8_t* _p;
		std::size_t         _n, _i{ 0 };
		bool                _ok{ true };
	};

	// The brain's world while recording: forwards every call to the real world and writes call + answer to the tape.
	class RecordingWorld final : public IWorld
	{
	public:
		void Begin(IWorld* a_inner, Bytes* a_tape) { _w = a_inner; _t = a_tape; _n = 0; }
		int  Calls() const { return _n; }
		bool ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps) override;
		bool Clear(const Vec3& a_from, const Vec3& a_to) override;
		bool Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor) override;
		bool Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac) override;
		int  OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		int  LockedDoorsSeen() override;
		int  SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		bool NavAvailable() override;
		bool NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out) override;
		// R6 step 8.1: the P1 hint reaches the real world (it changes no recorded answer: the ray's verdict is on the tape).
		// Since step 7 this wrapper swallowed it (IWorld's default no-op), so the dash target's P1 sight never ran in game;
		// whether the real world honours it is [Tabs] look.sightP1 (fo4/rays.cpp). LastRefused stays the default (false):
		// it would change the brain's answers without being on the tape (look.sightRefusedKeep is not shipped).
		void SightPriority(bool a_on) override
		{
			if (_w) _w->SightPriority(a_on);
		}

	private:
		IWorld* _w{ nullptr };
		Bytes*  _t{ nullptr };
		int     _n{ 0 };
	};

	// The brain's world in a replay: answers from the tape; a call that differs from the recording is a "tape miss"
	// (the replay diverged) and answers conservatively (blocked / no floor / no path).
	class TapeWorld final : public IWorld
	{
	public:
		void Begin(const std::uint8_t* a_p, std::size_t a_n) { _r = Reader(a_p, a_n); }
		int  Misses() const { return _miss; }
		bool Exhausted() const { return _r.Left() == 0; }
		bool ClearTo(const Vec3& a_from, const Vec3& a_to, std::uint32_t a_targetId, double a_reachEps) override;
		bool Clear(const Vec3& a_from, const Vec3& a_to) override;
		bool Floor(const Vec3& a_from, double a_maxDrop, Vec3& a_outFloor) override;
		bool Cast(const Vec3& a_from, const Vec3& a_to, double& a_hitFrac) override;
		int  OpenDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		int  LockedDoorsSeen() override;
		int  SmashDoors(const Vec3& a_from, const Vec3& a_to, double a_radius) override;
		bool NavAvailable() override;
		bool NavPath(const Vec3& a_fromFeet, const Vec3& a_toFeet, std::uint32_t a_targetId, double a_agentR, NavOut& a_out) override;

	private:
		bool Expect(Call a_c);
		Reader                 _r{ nullptr, 0 };
		int                    _miss{ 0 };
		std::deque<std::string> _strs;  // NavOut::why storage
	};

	// One decoded frame.
	struct Frame
	{
		double                    wall{ 0 };
		Facts                     facts{};  // facts.cands points into cands
		std::vector<Cand>         cands;
		std::uint64_t             paramsHash{ 0 };
		std::vector<std::uint8_t> ext;   // external calls before this Update (Ext records)
		std::vector<std::uint8_t> tape;  // IWorld calls of this Update
		DecisionKey               dec{};
		// R6 step 4 ([R6] Warden 2): the Warden's record of this frame (empty = the Warden did not pick this frame): its
		// external calls since its last Update, WFacts (+ candidates + reach estimates), its Params and the hash of
		// warden::DecisionKey(decision). DecodeWarden() reads it.
		std::vector<std::uint8_t> wrec;
	};

	// R6 step 4: one decoded Warden frame
	struct WFrame
	{
		std::vector<std::uint8_t>         ext;      // WExt records
		sp::warden::WFacts                facts{};  // cands / reach point into the vectors below
		std::vector<sp::warden::Cand>     cands;
		std::vector<sp::warden::ReachEst> reach;
		sp::warden::Params                params{};
		std::uint64_t                     key{ 0 };  // FNV-1a of warden::DecisionKey(d)
	};
	bool          DecodeWarden(const std::vector<std::uint8_t>& a_wrec, WFrame& a_out);
	std::uint64_t WardenKeyHash(const sp::warden::Decision& a_d);
	enum class WExt : std::uint8_t
	{
		kReset = 1, kOnLoad = 2, kOnJump = 3, kNoteHit = 4, kNoteKill = 5, kNoteSpared = 6, kNoteReachFail = 7, kNoteDefendJump = 8, kRecall = 9
	};

	struct Segment
	{
		std::shared_ptr<const Brain> key;     // the brain at the segment start (in memory only)
		// R6 step 4: the Warden at the segment start (in memory; SaveState bytes in the file); null = the segment started
		// while the Warden did not pick (its Warden frames are not replayable)
		std::shared_ptr<const sp::warden::Warden> wkey;
		std::vector<std::uint8_t>                 wkeyBytes;  // the file's copy (Parse)
		Params                       params{}; // the Params at the segment start
		std::vector<std::uint8_t>    packed;   // packed frame records
		std::vector<std::uint8_t>    last;     // the last raw record (the XOR base of the next one)
		double                       t0{ 0 }, t1{ 0 };
		std::uint32_t                frames{ 0 };
		bool                         fromReset{ false };  // the keyframe is a freshly Reset brain (a file replay can start from Reset)
		std::vector<std::uint8_t>    preExt;   // fromReset: the external calls between the Reset and the first frame (inside the
		                                       // keyframe already; a file replay from a fresh brain applies them first)
	};

	struct Snapshot
	{
		std::vector<Segment> segs;  // oldest first (1 or 2)
		std::string          why;
		double               wallAt{ 0 };
		double               brainAt{ 0 };
		std::uint32_t        seq{ 0 };
		bool                 Empty() const { return segs.empty() || segs.front().frames == 0; }
	};

	struct Config
	{
		bool   on{ false };
		double seconds{ 30 };
		double maxMB{ 2 };     // the dump file cap (the oldest segments are left out of the file first)
		double memMB{ 6 };     // the ring in memory (all segments): big fights at high fps keep their window
		int    segments{ 6 };  // >= 2
	};

	class Recorder
	{
	public:
		void Configure(const Config& a_c) { _cfg = a_c; }
		const Config& Cfg() const { return _cfg; }
		// Around one Brain::Update. Begin returns the world to pass to Update (records into this frame's tape).
		IWorld& Begin(const Brain& a_brain, const Facts& a_f, const Params& a_p, IWorld& a_inner, double a_wall);
		void    End(const Out& a_out);
		// External calls (recorded into the next frame; a Reset / OnLoad also marks the next keyframe "fromReset").
		void NoteReset();
		void NoteOnLoad();
		void NoteOnTeleport();
		void NoteOnOwnBlink(std::uint32_t a_target);
		void NoteHit(std::uint32_t a_id, double a_now, double a_dist);
		void NoteIgnore(std::uint32_t a_id, double a_now, double a_forS);
		void NoteRecall(bool a_fly, double a_now);
		void NoteSetVerdicts(std::span<const std::uint32_t> a_deny, std::span<const std::uint32_t> a_clear, double a_now, double a_hp);
		void NoteSpared(std::uint32_t a_id, double a_now);
		// R6 step 4 ([R6] Warden 2): the Warden's frame rides in the next brain record. WardenBegin BEFORE Warden::Update (when
		// a segment starts this frame, a_before becomes its Warden keyframe), WardenEnd after it; the Warden* notes are its
		// external calls (in the order they reach the Warden).
		void WardenBegin(const sp::warden::Warden& a_before, double a_now);
		void WardenEnd(const sp::warden::WFacts& a_f, const sp::warden::Params& a_p, const sp::warden::Decision& a_d);
		void WardenNoteHit(std::uint32_t a_id, double a_now);
		void WardenNoteKill(std::uint32_t a_id);
		void WardenNoteSpared(std::uint32_t a_id, double a_wall);
		void WardenNoteReachFail(std::uint32_t a_id, double a_now, const char* a_why);
		void WardenNoteDefendJump(double a_wall);
		void WardenRecall(bool a_fly, double a_now);
		void WardenOnJump();
		void WardenOnLoad();
		void WardenReset();
		// step 4 integrator ([Tabs] warden.actTickDt): true when the next brain record will start a new segment - the Warden
		// then ticks this frame, so the segment gets its Warden keyframe (a frame without a Warden record replays as before)
		bool WouldStartSegment(double a_now) const { return _cfg.on && NeedNew(a_now); }
		// A copy of the ring (game thread; the vectors are copied, the keyframes shared).
		Snapshot TakeSnapshot(const char* a_why, double a_wall) const;
		void     Clear();
		std::size_t   PackedBytes() const;
		std::uint64_t Frames() const { return _framesTotal; }
		double        LastRecordUs() const { return _lastUs; }
		double        AvgRecordUs() const { return _nUs ? _sumUs / static_cast<double>(_nUs) : 0.0; }  // mean over the session (Begin + End)
		std::uint64_t Segments() const { return _segsTotal; }

	private:
		void StartSegment(const Brain& a_brain, const Params& a_p, double a_t);
		Config               _cfg{};
		std::vector<Segment> _segs;  // <= 2
		sp::bb::Bytes        _ext, _tape, _rec;
		std::vector<std::uint8_t> _scratch;  // the XOR of this record with the last one (kept: no allocation per frame)
		RecordingWorld       _world;
		Facts                _f{};
		std::vector<Cand>    _cands;
		std::uint64_t        _paramsHash{ 0 };
		// step-2 fixer (COST): the params of the last hash; HashParams (FNV over the whole Params, ~5 us) runs only when a
		// byte differs (memcmp, ~0.2 us): the params change about once per second at most (R6Config)
		Params               _hashedP{};
		bool                 _haveHashedP{ false };
		bool                 _paramsChanged{ true };
		bool                 _inFrame{ false };
		bool                 _resetPending{ true };
		double               _wall{ 0 };
		std::uint64_t        _framesTotal{ 0 }, _segsTotal{ 0 };
		double               _lastUs{ 0 };
		double               _sumUs{ 0 };
		std::uint64_t        _nUs{ 0 };
		// R6 step 4: the Warden's part
		bool                 NeedNew(double a_now) const;
		sp::bb::Bytes        _wext, _wrec;
		bool                 _wHave{ false };       // _wrec holds this frame's Warden record
		bool                 _forceNew{ false };    // WardenBegin decided a new segment starts this frame
		std::shared_ptr<const sp::warden::Warden> _wPendingKey;
	};

	std::uint64_t HashParams(const Params& a_p);

	// ---- file ----
	// header + per segment {params, frames} ; a_maxBytes caps the file (the oldest segment is dropped first, then
	// frames from the front of the oldest kept one: marked notReplayable). Returns false on an empty snapshot.
	struct FileInfo
	{
		std::uint32_t version{ 0 };
		std::string   why;
		double        wallAt{ 0 }, brainAt{ 0 };
		std::uint32_t seq{ 0 };
		std::uint32_t segs{ 0 }, frames{ 0 };
		bool          replayableFromReset{ false };  // the first segment starts at a Brain::Reset (file replay possible)
		std::int32_t  inGameReplay{ -1 };            // the writer's own replay from the keyframe: -1 not run, 0 differed, 1 identical
		std::uint32_t inGameFrames{ 0 }, inGameMismatch{ 0 };
		std::uint32_t wFrames{ 0 };                  // R6 step 4: frames that carry a Warden record ([R6] Warden 2)
	};
	bool Serialize(const Snapshot& a_s, std::vector<std::uint8_t>& a_out, std::size_t a_maxBytes, const FileInfo* a_extra = nullptr);
	struct Parsed
	{
		FileInfo                         info;
		std::vector<Params>              segParams;  // per segment
		std::vector<std::vector<Frame>>  segFrames;
		std::vector<std::vector<std::uint8_t>> segPreExt;  // per segment (Segment::preExt)
		std::vector<std::vector<std::uint8_t>> segWKey;    // R6 step 4: per segment, the Warden keyframe bytes (empty = none)
		std::vector<std::vector<Params>> paramsChanges;  // unused slot (params changes are inline ext records)
	};
	bool Parse(const std::uint8_t* a_p, std::size_t a_n, Parsed& a_out, std::string* a_err = nullptr);

	// Decode the packed records of one in-memory segment (the writer's replay, tests).
	bool Decode(const Segment& a_seg, std::vector<Frame>& a_out);

	struct ReplayResult
	{
		std::uint32_t frames{ 0 }, mismatches{ 0 }, tapeMisses{ 0 };
		std::int64_t  firstBad{ -1 };
		// R6 step 4: the Warden's frames (replayed from the segment's Warden keyframe); wNoKey = Warden frames in segments
		// that started without one (not replayable)
		std::uint32_t wFrames{ 0 }, wMismatches{ 0 }, wNoKey{ 0 };
		std::int64_t  wFirstBad{ -1 };
		bool          Identical() const { return frames > 0 && mismatches == 0 && tapeMisses == 0 && wMismatches == 0; }
	};
	// R6 step 4: replays the Warden frames of one segment on a_w (the state at the segment start)
	ReplayResult ReplayWarden(sp::warden::Warden& a_w, std::span<const Frame> a_frames);
	// Replays frames in order on a_brain (the state at the first frame): ext calls, then Update with the recorded Facts,
	// the frame's Params (a_params, updated by inline params records) and the tape; compares every DecisionKey.
	ReplayResult Replay(Brain& a_brain, Params a_params, std::span<const Frame> a_frames);
	// Every segment of a snapshot from its own keyframe (the writer's in-game check).
	ReplayResult ReplaySnapshot(const Snapshot& a_s);
	// A parsed file: from a fresh Reset brain when the file says replayableFromReset, else from the first segment
	// with a fresh brain (decisions may differ until the brain state converges: reported, not "identical").
	ReplayResult ReplayFile(const Parsed& a_p);
	std::string  Summary(const Parsed& a_p, std::size_t a_maxFrames = 40);
}
