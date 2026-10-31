#pragma once
// OWNER: R6 BASELINE (new file).  The "that was weird" BOOKMARK key (round 6, user approved 2026-10-12).
//
// While he plays, the plugin keeps the last ~10 s of its own full-detail state in memory: one Frame per game frame (his
// target and why, the move / hunt / reach stage, route, stuck clocks, contact clocks, carry state, fps and our cost), the
// Events of that time (picks / switches with the reason the old target lost, give-ups, hunts, blinks, put-backs, swings,
// hits, refusals, kills, knock-downs, carry edges, teleports) and a candidate table 4 times a second (every foe the brain
// knows, its facts and why it is not the target). When the player presses the bookmark key, the runtime copies the window
// (Recorder::Grab), a worker thread formats it (Format) and writes
//   <Documents>\My Games\Fallout4\F4SE\SuperPeasant_Bookmarks\bookmark_<YYYYMMDD_HHMMSS_mmm>.log
// next to SuperPeasantNative.log, and the game shows "Super Peasant: moment saved".
//
// Pure C++ (no game headers): the rings, the capture, the text format, the summary, the file name and the key / pad
// chord trigger are tested on the desktop (tests/test_bookmark.inc). Cost while nothing is pressed: one Frame copy into a
// preallocated ring per frame (no allocation, no formatting, no I/O), measured in BK-6.
//
// THREADING: Push / Grab run on the F4SE task pump only (runtime::GameTick). Format / Summarize / FileName are pure and run
// on the writer thread on the Capture copy. Every const char* stored here must point to a string LITERAL (static storage):
// the brain's why / stage / hunt reasons and DownReason() are literals; anything dynamic goes into Event::text (copied).

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sp::bookmark
{
	// ---------------------------------------------------------------------------------------------- records
	// Frame flag bits
	enum FrameFlag : std::uint32_t
	{
		kFBrainOn = 1u << 0,         // the native brain decided this frame
		kFDown = 1u << 1,            // he is down (dead / bleedout / ragdoll / getting up): no sim, no swings
		kFIndoors = 1u << 2,         // indoor caps on (interior cell or a low ceiling, with hysteresis)
		kFInterior = 1u << 3,        // his own cell is an interior
		kFLowCeiling = 1u << 4,      // "indoors" because of a low ceiling outdoors
		kFVisible = 1u << 5,         // the brain sees its target
		kFMayAttack = 1u << 6,       // visible and within swing range (the attack gate may swing)
		kFEscort = 1u << 7,          // escort (vertibird) or carry: no fights
		kFFollow = 1u << 8,          // the native flying follow is on
		kFFollowBlocked = 1u << 9,   // no clear line from his sim to a follow spot
		kFTabsChain = 1u << 10,      // the TABS dash chain alone moves him this frame
		kFHuntMech = 1u << 11,       // far-leg mechanism (the capsule leads at the cruise law)
		kFDirectClear = 1u << 12,    // the straight line to the target was clear (last 0.15 s check)
		kFBlink = 1u << 13,          // the brain asked for a blink this frame
		kFPutBack = 1u << 14,        // the brain asked for a put-back this frame
		kFTargetChanged = 1u << 15,  // the target changed this frame
		kFPin = 1u << 16,            // the target pins the player
		kFClimb = 1u << 17,          // terrain clearance / look-ahead climb
		kFChaseBoost = 1u << 18,     // the AI-chase stage of the reach chain
		kFBlinkAttack = 1u << 19,    // attacking straight after his own blink
		kFBodyReady = 1u << 20,      // the TABS sim runs (mode >= 2, bound)
		kFLoaded = 1u << 21,         // he has 3D in a cell
		kFCarryNoFight = 1u << 22,   // the carry keeps the brain idle
		kFMoverBlocked = 1u << 23,   // the mover's last feedback window was blocked (latched)
	};

	struct Frame
	{
		double        t{ 0 };                 // s, the runtime clock (QPC)
		std::uint64_t frame{ 0 };
		float         dt{ 0 };                // s, the game frame
		float         costMs{ -1 };           // our GameTick cost this frame so far (P5), ms; -1 unknown
		float         costP6Ms{ -1 };         // our last MoveProxy (P6) cost, ms
		std::int8_t   mode{ 0 };              // ini / Papyrus mode 0..3
		std::uint32_t flags{ 0 };             // FrameFlag bits
		const char*   down{ nullptr };        // DownReason literal (nullptr = up)
		// the brain's decision
		std::uint32_t target{ 0 };
		std::uint8_t  tier{ 0 }, cls{ 0 }, move{ 0 }, hunt{ 0 };  // sp::brain::Tier / Cls / Move / HuntState values
		std::int8_t   reachStage{ -1 };       // -1 none, 0 nav, 1 ray, 2 AI, 3 blink, 4 given up
		std::int8_t   planKind{ 0 };
		std::uint8_t  giveUp{ 0 };            // sp::brain::GiveUp value of a give-up this frame
		std::uint8_t  blinkWhy{ 0 };
		std::int16_t  navIdx{ 0 }, navN{ 0 };
		std::uint32_t giveUpId{ 0 };
		const char*   stageWhy{ nullptr };    // literal
		float         dist{ 0 };              // u, his chest -> target chest
		float         navRemain{ 0 };         // u left on the nav route
		float         vDes{ 0 }, vMax{ 0 };   // u/s commanded speed and cap
		float         sight{ 0 };             // the attack gate's sight timer
		std::uint16_t rays{ 0 }, cands{ 0 };  // brain rays this frame; candidates the brain knows
		// movement / stuck / contact clocks
		float         blockedFor{ 0 };        // s the mover has been blocked without a break
		float         contactBlockedFor{ 0 }; // s near the target with every contact line from his sim blocked
		float         aroundBlockedFor{ 0 };  // s within punch.aroundR with every contact line blocked
		float         inSolidFor{ 0 };        // s his sim has been inside geometry
		float         nearNoHitFor{ 0 };      // s within punch.lungeR of the target without a hit
		float         headroom{ 0 };          // u, ray up from his chest
		float         wingsDist{ -1 };        // u, the Wings floor ray (-1 = none)
		std::uint8_t  flightMode{ 0 };        // sp::flight::Mode value
		std::uint8_t  bodyState{ 0 };         // sp::body::AnimState value
		std::uint8_t  carryState{ 0 };        // 0 OFF 1 GRAB 2 CARRY 3 LAND 4 GUARD
		std::int8_t   carryWhy{ -1 };
		std::uint16_t flings{ 0 };            // flings in flight
		std::uint32_t swings{ 0 }, hits{ 0 }; // cumulative native counters
		// positions (FO4 u) and velocities (u/s)
		float         me[3]{}, meVel[3]{}, sim[3]{}, player[3]{};
		float         playerHp{ -1 };         // 0..1 (sampled 10 times a second); -1 unknown
	};

	enum class Ev : std::uint8_t
	{
		kNote = 0,   // anything else (text)
		kPress,      // the bookmark key itself
		kBrain,      // brain on / off (text = why)
		kGate,       // Papyrus gate flags (i = new flags, a = old)
		kTarget,     // pick / switch: a = new target, b = old target, x = distance, text = tier/cls/vis/move + why the old one lost
		kGiveUp,     // a = foe, text = reason
		kHuntStart,  // a = foe, x = distance
		kHuntAttack,
		kHuntEnd,    // a = foe, text = why
		kReturn,     // the return leg started
		kStage,      // reach stage edge: a = foe, i = new stage, text = from->to why
		kChase,      // AI chase asked: a = foe, text = sent / refused why
		kPutBack,    // i = why code
		kBlink,      // a = foe (0 = the D2 return blink), i = why code, x = distance
		kTeleport,   // his actor jumped: x = distance, text = what
		kSwing,      // a = target, i = fist
		kHit,        // a = victim, i = fist, x = k, text = mechanism
		kVeto,       // a = actor, text = where: why (a hit or a pick refused)
		kKill,       // a = victim
		kSpared,     // an essential knocked down: a = foe
		kDown,       // he went down (text = why)
		kUp,         // he got up
		kCarry,      // carry state edge: i = new state, b = old state
		kVerdicts,   // Papyrus SetVerdicts: i = denies, b = clears, x = player health share
		kRecall,     // Papyrus Recall / whistle
		kWait,       // Papyrus wait on / off (i)
		kAttacker,   // Papyrus reported an attacker (OnHit on the player / him / a teammate): a = attacker, i = kind
		kCount
	};
	const char* EvName(Ev a_e);

	struct Event
	{
		double        t{ 0 };
		std::uint64_t frame{ 0 };
		Ev            kind{ Ev::kNote };
		std::int32_t  i{ 0 };
		std::uint32_t a{ 0 }, b{ 0 };
		float         x{ 0 };
		char          text[72]{};  // copied (any lifetime)
	};
	// a_text is copied (truncated to 71 chars); nullptr = empty
	Event MakeEvent(double a_t, std::uint64_t a_frame, Ev a_kind, std::uint32_t a_a = 0, std::uint32_t a_b = 0, std::int32_t a_i = 0, float a_x = 0,
		const char* a_text = nullptr);

	// candidate flag bits
	enum CandFlag : std::uint32_t
	{
		kCHostile = 1u << 0,
		kCCombatGroup = 1u << 1,     // its combat target is the player / him / a teammate
		kCCombatPlayer = 1u << 2,
		kCAttackingPlayer = 1u << 3, // hit the player within the defend window or targets the player
		kCDefend = 1u << 4,          // attacking the group now (soft vetoes lifted)
		kCEssential = 1u << 5,
		kCEssentialOnly = 1u << 6,
		kCDenied = 1u << 7,          // a Papyrus / scan deny
		kCCleared = 1u << 8,
		kCLoaded = 1u << 9,
		kCKillPending = 1u << 10,
		kCNativeDeny = 1u << 11,
		kCQuest = 1u << 12,          // quest alias / unique
		kCWeaponDrawn = 1u << 13,
		kCTarget = 1u << 14,         // it is the target right now
		kCInCombat = 1u << 15,
	};

	struct Cand
	{
		double        t{ 0 };
		std::uint32_t id{ 0 };
		std::uint8_t  cls{ 0 };
		std::uint32_t flags{ 0 };
		float         dMe{ 0 }, dPlayer{ 0 };   // u
		float         hitGroupAgo{ -1 };        // s since it hit the player / him / a teammate (-1 never / long ago)
		const char*   whyNot{ nullptr };        // literal: why it is not eligible ("" = eligible)
	};

	// ---------------------------------------------------------------------------------------------- the ring
	// Fixed capacity, preallocated once: Push never allocates. Oldest entries are overwritten.
	template <class T>
	class Ring
	{
	public:
		void Reserve(std::size_t a_cap)
		{
			_buf.assign(a_cap > 0 ? a_cap : 1, T{});
			_head = 0;
			_n = 0;
		}
		std::size_t Capacity() const { return _buf.size(); }
		std::size_t Size() const { return _n; }
		void        Clear()
		{
			_head = 0;
			_n = 0;
		}
		void Push(const T& a_v)
		{
			if (_buf.empty()) return;
			_buf[_head] = a_v;
			_head = (_head + 1) % _buf.size();
			if (_n < _buf.size()) ++_n;
		}
		// i = 0 is the oldest kept entry
		const T& At(std::size_t a_i) const { return _buf[(_head + _buf.size() - _n + a_i) % _buf.size()]; }
		// the entries with t >= a_t0, oldest first, appended to a_out
		void CopySince(double a_t0, std::vector<T>& a_out) const
		{
			std::size_t first = 0;
			while (first < _n && At(first).t < a_t0) ++first;
			a_out.reserve(a_out.size() + (_n - first));
			for (std::size_t i = first; i < _n; ++i) a_out.push_back(At(i));
		}

	private:
		std::vector<T> _buf;
		std::size_t    _head{ 0 }, _n{ 0 };
	};

	// ---------------------------------------------------------------------------------------------- recorder / capture
	struct Capture
	{
		double                   tPress{ 0 };     // runtime clock of the key press (the last frame's t)
		double                   window{ 10 };    // s kept before the press
		std::string              when;            // local wall-clock time of the press, "2026-10-12 21:04:55.123"
		std::string              how;             // "keyboard F10", "pad View+Y", "test"
		std::vector<std::string> header;          // free lines (build, ini, state at the press)
		std::vector<Frame>       frames;
		std::vector<Event>       events;
		std::vector<Cand>        cands;
		std::vector<std::string> log;             // SuperPeasantNative.log lines of the window (time-stamped)
	};

	class Recorder
	{
	public:
		// frames: ~10 s at up to 400 fps; events / candidate rows: a busy fight
		void Configure(std::size_t a_frames = 4096, std::size_t a_events = 4096, std::size_t a_cands = 8192);
		bool Configured() const { return _frames.Capacity() > 1; }
		void Clear();
		void Push(const Frame& a_f)
		{
			_frames.Push(a_f);
			++_framesPushed;
		}
		void Push(const Event& a_e)
		{
			_events.Push(a_e);
			++_eventsPushed;
		}
		void Push(const Cand& a_c) { _cands.Push(a_c); }
		// copies everything of the last a_window s before a_now into a_out (frames / events / cands)
		void Grab(double a_now, double a_window, Capture& a_out) const;

		std::uint64_t FramesPushed() const { return _framesPushed; }
		std::uint64_t EventsPushed() const { return _eventsPushed; }
		std::size_t   FramesKept() const { return _frames.Size(); }

	private:
		Ring<Frame>   _frames;
		Ring<Event>   _events;
		Ring<Cand>    _cands;
		std::uint64_t _framesPushed{ 0 }, _eventsPushed{ 0 };
	};

	// ---------------------------------------------------------------------------------------------- report
	struct TargetStat
	{
		std::uint32_t id{ 0 };
		double        seconds{ 0 };   // time it was the target
		double        visibleS{ 0 };  // of which visible
		double        firstT{ 0 };    // first frame it was the target (relative to the press, s, <= 0)
		int           swings{ 0 }, hits{ 0 };
		double        minDist{ -1 };
	};
	struct Summary
	{
		double                  seconds{ 0 };        // covered by frames
		int                     frames{ 0 };
		double                  fpsAvg{ 0 }, fpsMin{ 0 };
		double                  costAvgMs{ 0 }, costMaxMs{ 0 };
		int                     switches{ 0 };       // target changes (to another non-zero target)
		int                     picks{ 0 };          // 0 -> target
		int                     drops{ 0 };          // target -> 0
		int                     swings{ 0 }, hits{ 0 };
		int                     giveUps{ 0 }, blinks{ 0 }, putBacks{ 0 }, vetoes{ 0 }, kills{ 0 }, teleports{ 0 };
		double                  idleS{ 0 };          // no target while a candidate attacked the player (seen in the table)
		double                  blockedMaxS{ 0 }, contactBlockedMaxS{ 0 }, nearNoHitMaxS{ 0 };
		double                  playerHpMin{ -1 }, playerHpLast{ -1 };
		double                  downS{ 0 };
		std::vector<TargetStat> targets;             // by time as the target, most first
	};
	Summary Summarize(const Capture& a_c);

	// the whole text of the bookmark file
	std::string Format(const Capture& a_c);
	// "bookmark_20261012_210455_123.log"
	std::string FileName(int a_year, int a_month, int a_day, int a_hour, int a_min, int a_sec, int a_ms);
	// "F10", "G", "Num5", "VK 0x7A"
	std::string KeyName(int a_vk);
	// "View+Y", "LB+RB"
	std::string PadName(std::uint32_t a_mask);

	// ---------------------------------------------------------------------------------------------- the trigger
	// Keyboard: the press edge of the key (with the modifier held when one is set). Pad: every button of padMask held at the
	// same time, the last of them pressed within padWindow s of the first. Both: not in menus (the observer drops them),
	// at most one bookmark per debounce s.
	struct TriggerCfg
	{
		int           key{ 119 };          // VK (119 = F8, unused by Fallout 4); 0 = no keyboard key
		int           keyMod{ 0 };         // VK that must be held too (0 = none)
		std::uint32_t padMask{ 0x8020 };   // XInput buttons that must be held together (0x20 View / Back + 0x8000 Y); 0 = off
		double        padWindow{ 0.5 };    // s between the first and the last press of the chord
		double        debounce{ 2.0 };     // s
	};
	class Trigger
	{
	public:
		void SetCfg(const TriggerCfg& a_c) { _c = a_c; }
		const TriggerCfg& Cfg() const { return _c; }
		void Reset();
		// keyboard edge (VK code). Returns true when this edge fires a bookmark. a_fresh = the engine says this is a new press
		// (ButtonEvent heldDownSecs == 0): it counts even when the release before it was lost (alt-tab, a menu).
		bool Key(int a_vk, bool a_down, double a_t, bool a_fresh = false);
		// pad button edge (XInput mask bit). Returns true when this edge fires a bookmark.
		bool Pad(std::uint32_t a_bit, bool a_down, double a_t, bool a_fresh = false);
		int  Fired() const { return _fired; }
		int  Debounced() const { return _debounced; }

	private:
		bool Fire(double a_t);
		TriggerCfg            _c{};
		bool                  _modDown{ false }, _keyDown{ false };
		std::uint32_t         _padHeld{ 0 };
		std::array<double, 32> _padDownT{};
		double                _lastFire{ -1e9 };
		int                   _fired{ 0 }, _debounced{ 0 };
	};
}
