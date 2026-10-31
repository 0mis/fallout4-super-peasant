// OWNER: R6 BASELINE (new file).  See bookmark.h.
#include "core/bookmark.h"

#include "core/body.h"
#include "core/brain.h"
#include "core/carry.h"
#include "core/flight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <map>

namespace sp::bookmark
{
	namespace
	{
		const char* Safe(const char* s) { return (s && s[0]) ? s : "-"; }

		const char* HuntName(int h)
		{
			switch (h) {
			case 0: return "idle";
			case 1: return "local";
			case 2: return "far";
			case 3: return "return";
			case 4: return "aichase";
			default: return "?";
			}
		}
		const char* MoveLong(int m)
		{
			switch (m) {
			case 0: return "follow";
			case 1: return "approach";
			case 2: return "hunt";
			case 3: return "overtop";
			case 4: return "aichase";
			case 5: return "return";
			case 6: return "hold";
			case 7: return "nav";
			default: return "?";
			}
		}
		const char* StageName(int s)
		{
			switch (s) {
			case -1: return "-";
			case 0: return "nav";
			case 1: return "ray";
			case 2: return "ai";
			case 3: return "blink";
			case 4: return "givenup";
			case 5: return "route";  // R6 step 9 (sp::brain::kRouteStage: the Router drives)
			default: return "?";
			}
		}
		const char* BodyName(int b)
		{
			switch (b) {
			case 0: return "idle";
			case 1: return "walk";
			case 2: return "run";
			case 3: return "strafe";
			default: return "?";
			}
		}

		// one letter per FrameFlag bit, in bit order (legend in the file)
		constexpr const char kFlagLetters[] = "BDIicVMEFfTHLKPSNCAaROYW";
		constexpr const char* kFlagLegend =
			"B brain on, D down, I indoors, i interior cell, c low ceiling, V target visible, M may attack, E escort/carry (no fights), "
			"F follow on, f follow spot blocked, T TABS dash chain moves him, H far-leg mechanism, L straight line clear, K blink asked, "
			"P put-back asked, S target changed, N target pins the player, C climbing, A AI-chase stage, a attack after his blink, "
			"R TABS body running, O loaded, Y carry keeps the brain idle, W mover blocked";

		std::string FlagStr(std::uint32_t f)
		{
			std::string s;
			for (int i = 0; i < 24; ++i)
				if (f & (1u << i)) s += kFlagLetters[i];
			if (s.empty()) s = "-";
			return s;
		}

		std::string CandFlags(std::uint32_t f)
		{
			static constexpr const char* kN[] = { "hostile", "cGroup", "cPlayer", "atkPlayer", "defend", "essential", "essOnly", "denied", "cleared", "loaded",
				"killPending", "nativeDeny", "quest", "weapon", "TARGET", "inCombat" };
			std::string s;
			for (int i = 0; i < 16; ++i)
				if (f & (1u << i)) {
					if (!s.empty()) s += ',';
					s += kN[i];
				}
			return s.empty() ? std::string("-") : s;
		}

		template <class... A>
		void Out(std::string& o, std::format_string<A...> f, A&&... a)
		{
			std::format_to(std::back_inserter(o), f, std::forward<A>(a)...);
		}
	}

	const char* EvName(Ev e)
	{
		switch (e) {
		case Ev::kNote: return "note";
		case Ev::kPress: return "PRESS";
		case Ev::kBrain: return "brain";
		case Ev::kGate: return "gate";
		case Ev::kTarget: return "target";
		case Ev::kGiveUp: return "giveup";
		case Ev::kHuntStart: return "hunt+";
		case Ev::kHuntAttack: return "huntAtk";
		case Ev::kHuntEnd: return "hunt-";
		case Ev::kReturn: return "return";
		case Ev::kStage: return "stage";
		case Ev::kChase: return "chase";
		case Ev::kPutBack: return "putback";
		case Ev::kBlink: return "blink";
		case Ev::kTeleport: return "teleport";
		case Ev::kSwing: return "swing";
		case Ev::kHit: return "HIT";
		case Ev::kVeto: return "refused";
		case Ev::kKill: return "kill";
		case Ev::kSpared: return "essDown";
		case Ev::kDown: return "DOWN";
		case Ev::kUp: return "up";
		case Ev::kCarry: return "carry";
		case Ev::kVerdicts: return "verdicts";
		case Ev::kRecall: return "recall";
		case Ev::kWait: return "wait";
		case Ev::kAttacker: return "attacker";
		default: return "?";
		}
	}

	Event MakeEvent(double a_t, std::uint64_t a_frame, Ev a_kind, std::uint32_t a_a, std::uint32_t a_b, std::int32_t a_i, float a_x, const char* a_text)
	{
		Event e;
		e.t = a_t;
		e.frame = a_frame;
		e.kind = a_kind;
		e.a = a_a;
		e.b = a_b;
		e.i = a_i;
		e.x = a_x;
		if (a_text) {
			std::size_t n = 0;
			for (; n + 1 < sizeof(e.text) && a_text[n]; ++n) e.text[n] = a_text[n];
			e.text[n] = 0;
		}
		return e;
	}

	// ---------------------------------------------------------------------------------------------- recorder
	void Recorder::Configure(std::size_t a_frames, std::size_t a_events, std::size_t a_cands)
	{
		_frames.Reserve(std::clamp<std::size_t>(a_frames, 64, 1u << 16));
		_events.Reserve(std::clamp<std::size_t>(a_events, 64, 1u << 16));
		_cands.Reserve(std::clamp<std::size_t>(a_cands, 64, 1u << 17));
		_framesPushed = _eventsPushed = 0;
	}

	void Recorder::Clear()
	{
		_frames.Clear();
		_events.Clear();
		_cands.Clear();
	}

	void Recorder::Grab(double a_now, double a_window, Capture& a_out) const
	{
		const double t0 = a_now - std::max(0.0, a_window);
		a_out.tPress = a_now;
		a_out.window = a_window;
		a_out.frames.clear();
		a_out.events.clear();
		a_out.cands.clear();
		_frames.CopySince(t0, a_out.frames);
		_events.CopySince(t0, a_out.events);
		_cands.CopySince(t0, a_out.cands);
	}

	// ---------------------------------------------------------------------------------------------- summary
	Summary Summarize(const Capture& c)
	{
		Summary s;
		s.frames = static_cast<int>(c.frames.size());
		if (!c.frames.empty()) s.seconds = c.frames.back().t - c.frames.front().t + c.frames.back().dt;
		std::map<std::uint32_t, TargetStat> ts;
		double                              fpsSum = 0, costSum = 0;
		int                                 fpsN = 0, costN = 0;
		s.fpsMin = 0;
		double       blocked = 0, ctBlk = 0, nearNoHit = 0;
		std::size_t  ci = 0;          // candidate rows walked in time order
		double       lastCandT = -1e18;
		bool         attackerSeen = false;  // the last candidate snapshot had a foe attacking the player
		std::uint32_t prev = c.frames.empty() ? 0 : c.frames.front().target;
		bool          first = true;
		for (const auto& f : c.frames) {
			const double dt = (f.dt > 0 && f.dt < 1.0f) ? f.dt : 0.0;
			if (dt > 0) {
				const double fps = 1.0 / dt;
				fpsSum += fps;
				++fpsN;
				s.fpsMin = fpsN == 1 ? fps : std::min(s.fpsMin, fps);
			}
			if (f.costMs >= 0) {
				costSum += f.costMs;
				++costN;
				s.costMaxMs = std::max(s.costMaxMs, static_cast<double>(f.costMs));
			}
			if (f.flags & kFDown) s.downS += dt;
			// candidate snapshots up to this frame
			while (ci < c.cands.size() && c.cands[ci].t <= f.t) {
				const double ct = c.cands[ci].t;
				if (ct != lastCandT) {
					lastCandT = ct;
					attackerSeen = false;
				}
				if (c.cands[ci].flags & kCAttackingPlayer) attackerSeen = true;
				++ci;
			}
			if (f.target == 0 && (f.flags & kFBrainOn) && !(f.flags & kFDown) && !(f.flags & kFEscort) && attackerSeen && f.t - lastCandT < 0.6) s.idleS += dt;
			if (!first && f.target != prev) {
				if (prev == 0) ++s.picks;
				else if (f.target == 0) ++s.drops;
				else ++s.switches;
			}
			first = false;
			prev = f.target;
			if (f.target) {
				auto& t = ts[f.target];
				if (t.id == 0) {
					t.id = f.target;
					t.firstT = f.t - c.tPress;
				}
				t.seconds += dt;
				if (f.flags & kFVisible) t.visibleS += dt;
				if (f.dist > 0) t.minDist = t.minDist < 0 ? f.dist : std::min(t.minDist, static_cast<double>(f.dist));
			}
			blocked = std::max(blocked, static_cast<double>(f.blockedFor));
			ctBlk = std::max(ctBlk, static_cast<double>(f.contactBlockedFor));
			nearNoHit = std::max(nearNoHit, static_cast<double>(f.nearNoHitFor));
			if (f.playerHp >= 0) {
				s.playerHpMin = s.playerHpMin < 0 ? f.playerHp : std::min(s.playerHpMin, static_cast<double>(f.playerHp));
				s.playerHpLast = f.playerHp;
			}
		}
		s.fpsAvg = fpsN ? fpsSum / fpsN : 0.0;
		s.costAvgMs = costN ? costSum / costN : 0.0;
		s.blockedMaxS = blocked;
		s.contactBlockedMaxS = ctBlk;
		s.nearNoHitMaxS = nearNoHit;
		for (const auto& e : c.events) {
			switch (e.kind) {
			case Ev::kSwing:
				++s.swings;
				if (e.a) {
					auto& t = ts[e.a];
					if (t.id == 0) {
						t.id = e.a;
						t.firstT = e.t - c.tPress;
					}
					++t.swings;
				}
				break;
			case Ev::kHit:
				++s.hits;
				if (e.a) {
					auto& t = ts[e.a];
					if (t.id == 0) {
						t.id = e.a;
						t.firstT = e.t - c.tPress;
					}
					++t.hits;
				}
				break;
			case Ev::kGiveUp: ++s.giveUps; break;
			case Ev::kBlink: ++s.blinks; break;
			case Ev::kPutBack: ++s.putBacks; break;
			case Ev::kVeto: ++s.vetoes; break;
			case Ev::kKill: ++s.kills; break;
			case Ev::kTeleport: ++s.teleports; break;
			default: break;
			}
		}
		for (auto& [id, t] : ts) s.targets.push_back(t);
		std::stable_sort(s.targets.begin(), s.targets.end(), [](const TargetStat& a, const TargetStat& b) { return a.seconds > b.seconds; });
		return s;
	}

	// ---------------------------------------------------------------------------------------------- the text
	std::string Format(const Capture& c)
	{
		std::string o;
		o.reserve(256 + c.frames.size() * 420 + c.events.size() * 120 + c.cands.size() * 140 + c.log.size() * 160);
		const Summary s = Summarize(c);
		Out(o, "Super Peasant bookmark (\"that was weird\")\n");
		Out(o, "saved      {} (local time)\n", c.when.empty() ? std::string("?") : c.when);
		Out(o, "trigger    {}\n", c.how.empty() ? std::string("?") : c.how);
		Out(o, "window     the last {:.1f} s before the press; times below are seconds BEFORE the press (-2.500 = 2.5 s before)\n", c.window);
		if (!c.frames.empty()) Out(o, "frames     {} .. {}\n", c.frames.front().frame, c.frames.back().frame);
		for (const auto& h : c.header) Out(o, "{}\n", h);

		Out(o, "\n== SUMMARY ==\n");
		Out(o, "{} frames over {:.2f} s, fps avg {:.1f} min {:.1f}, our cost avg {:.3f} ms max {:.2f} ms\n", s.frames, s.seconds, s.fpsAvg, s.fpsMin, s.costAvgMs, s.costMaxMs);
		Out(o, "targets: {} switches, {} picks from nothing, {} drops to nothing; {:.2f} s without a target while a foe attacked the player\n", s.switches, s.picks, s.drops, s.idleS);
		Out(o, "swings {}, hits {}, give-ups {}, blinks {}, put-backs {}, refusals {}, kills {}, teleports {}, down {:.2f} s\n", s.swings, s.hits, s.giveUps, s.blinks, s.putBacks,
			s.vetoes, s.kills, s.teleports, s.downS);
		Out(o, "longest: mover blocked {:.2f} s, contact lines blocked {:.2f} s, near the target without a hit {:.2f} s\n", s.blockedMaxS, s.contactBlockedMaxS, s.nearNoHitMaxS);
		if (s.playerHpMin >= 0) Out(o, "player health: lowest {:.0f} %, at the press {:.0f} %\n", s.playerHpMin * 100.0, s.playerHpLast * 100.0);
		if (!s.targets.empty()) {
			Out(o, "his targets (most time first):\n");
			for (const auto& t : s.targets)
				Out(o, "  {:08X}  target {:.2f} s (visible {:.2f} s), first seen {:+.3f} s, swings {}, hits {}, closest {:.0f} u\n", t.id, t.seconds, t.visibleS, t.firstT, t.swings,
					t.hits, t.minDist);
		}

		Out(o, "\n== EVENTS ({}) ==\n", c.events.size());
		Out(o, "      t   frame     event     a        b        i      x  text\n");
		for (const auto& e : c.events)
			Out(o, "{:+8.3f} {:7} {:<9} {:08X} {:08X} {:4} {:8.1f}  {}\n", e.t - c.tPress, e.frame, EvName(e.kind), e.a, e.b, e.i, e.x, e.text);

		Out(o, "\n== CANDIDATES (4 snapshots a second; d = distance to him, dPl = to the player, hit = s since it hit the player / him / a teammate) ==\n");
		for (const auto& r : c.cands)
			Out(o, "{:+8.3f} {}{:08X} cls={} d={:.0f} dPl={:.0f} hit={} why={} [{}]\n", r.t - c.tPress, (r.flags & kCTarget) ? '*' : ' ', r.id,
				sp::brain::ClsName(static_cast<sp::brain::Cls>(r.cls)), r.dMe, r.dPlayer, r.hitGroupAgo >= 0 ? std::format("{:.2f}", r.hitGroupAgo) : std::string("-"),
				(r.whyNot && r.whyNot[0]) ? r.whyNot : "eligible", CandFlags(r.flags));

		Out(o, "\n== FRAMES (every frame) ==\n");
		Out(o, "flags: {}\n", kFlagLegend);
		Out(o, "t,frame,fps,costMs,p6Ms,mode,flags,down,target,tier,cls,dist,move,hunt,stage,stageWhy,plan,nav,navRemain,vDes,vMax,sight,rays,cands,giveUp,"
		       "blocked,ctBlocked,aroundBlocked,inSolid,nearNoHit,headroom,wings,flight,body,carry,carryWhy,flings,swings,hits,"
		       "me_x,me_y,me_z,vel_x,vel_y,vel_z,sim_x,sim_y,sim_z,pl_x,pl_y,pl_z,plHp\n");
		for (const auto& f : c.frames) {
			const double fps = (f.dt > 0) ? 1.0 / f.dt : 0.0;
			Out(o, "{:+.4f},{},{:.1f},{:.3f},{:.3f},{},{},{},{:08X},{},{},{:.0f},{},{},{},{},{},{}/{},{:.0f},{:.0f},{:.0f},{:.2f},{},{},", f.t - c.tPress, f.frame, fps, f.costMs,
				f.costP6Ms, static_cast<int>(f.mode), FlagStr(f.flags), Safe(f.down), f.target, sp::brain::TierName(static_cast<sp::brain::Tier>(f.tier)),
				sp::brain::ClsName(static_cast<sp::brain::Cls>(f.cls)), f.dist, MoveLong(f.move), HuntName(f.hunt), StageName(f.reachStage), Safe(f.stageWhy),
				static_cast<int>(f.planKind), f.navIdx, f.navN, f.navRemain, f.vDes, f.vMax, f.sight, f.rays, f.cands);
			if (f.giveUp) Out(o, "{}:{:08X},", sp::brain::GiveUpName(static_cast<sp::brain::GiveUp>(f.giveUp)), f.giveUpId);
			else o += "-,";
			Out(o, "{:.2f},{:.2f},{:.2f},{:.2f},{:.2f},{:.0f},{:.0f},{},{},{},{},{},{},{},", f.blockedFor, f.contactBlockedFor, f.aroundBlockedFor, f.inSolidFor, f.nearNoHitFor,
				f.headroom, f.wingsDist, sp::flight::ModeName(static_cast<sp::flight::Mode>(f.flightMode)), BodyName(f.bodyState),
				sp::carry::StateName(static_cast<sp::carry::State>(f.carryState)), static_cast<int>(f.carryWhy), f.flings, f.swings, f.hits);
			Out(o, "{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{:.0f},{}\n", f.me[0], f.me[1], f.me[2], f.meVel[0], f.meVel[1], f.meVel[2], f.sim[0],
				f.sim[1], f.sim[2], f.player[0], f.player[1], f.player[2], f.playerHp >= 0 ? std::format("{:.3f}", f.playerHp) : std::string("-"));
		}

		Out(o, "\n== LOG (SuperPeasantNative.log lines of the window) ==\n");
		for (const auto& l : c.log) {
			o += l;
			if (l.empty() || l.back() != '\n') o += '\n';
		}
		Out(o, "\n== END ==\n");
		return o;
	}

	std::string FileName(int y, int mo, int d, int h, int mi, int s, int ms)
	{
		return std::format("bookmark_{:04}{:02}{:02}_{:02}{:02}{:02}_{:03}.log", y, mo, d, h, mi, s, std::clamp(ms, 0, 999));
	}

	std::string KeyName(int vk)
	{
		if (vk >= 0x70 && vk <= 0x87) return std::format("F{}", vk - 0x70 + 1);
		if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return std::string(1, static_cast<char>(vk));
		if (vk >= 0x60 && vk <= 0x69) return std::format("Num{}", vk - 0x60);
		switch (vk) {
		case 0x13: return "Pause";
		case 0x24: return "Home";
		case 0x23: return "End";
		case 0x2D: return "Insert";
		case 0x2E: return "Delete";
		case 0x21: return "PageUp";
		case 0x22: return "PageDown";
		case 0x91: return "ScrollLock";
		case 0xA0: return "LShift";
		case 0xA1: return "RShift";
		case 0xA2: return "LCtrl";
		case 0xA3: return "RCtrl";
		case 0xA4: return "LAlt";
		case 0xA5: return "RAlt";
		default: return std::format("VK 0x{:02X}", vk);
		}
	}

	std::string PadName(std::uint32_t m)
	{
		static constexpr struct
		{
			std::uint32_t bit;
			const char*   n;
		} kB[] = { { 0x0001, "DpadUp" }, { 0x0002, "DpadDown" }, { 0x0004, "DpadLeft" }, { 0x0008, "DpadRight" }, { 0x0010, "Start" }, { 0x0020, "View" },
			{ 0x0040, "LS" }, { 0x0080, "RS" }, { 0x0100, "LB" }, { 0x0200, "RB" }, { 0x1000, "A" }, { 0x2000, "B" }, { 0x4000, "X" }, { 0x8000, "Y" } };
		std::string s;
		for (const auto& b : kB)
			if (m & b.bit) {
				if (!s.empty()) s += '+';
				s += b.n;
			}
		return s.empty() ? std::string("off") : s;
	}

	// ---------------------------------------------------------------------------------------------- trigger
	void Trigger::Reset()
	{
		_modDown = _keyDown = false;
		_padHeld = 0;
		_padDownT = {};
	}

	bool Trigger::Fire(double t)
	{
		if (t - _lastFire < _c.debounce) {
			++_debounced;
			return false;
		}
		_lastFire = t;
		++_fired;
		return true;
	}

	bool Trigger::Key(int vk, bool down, double t, bool fresh)
	{
		if (vk <= 0) return false;
		if (_c.keyMod > 0 && vk == _c.keyMod) _modDown = down;
		if (_c.key <= 0 || vk != _c.key) return false;
		const bool edge = down && (!_keyDown || fresh);
		_keyDown = down;
		if (!edge) return false;  // held-key repeats are no new press
		if (_c.keyMod > 0 && !_modDown) return false;
		return Fire(t);
	}

	bool Trigger::Pad(std::uint32_t bit, bool down, double t, bool fresh)
	{
		if (bit == 0 || (bit & (bit - 1)) != 0) return false;  // one button per edge
		int idx = 0;
		while (idx < 31 && !((bit >> idx) & 1u)) ++idx;
		const bool was = (_padHeld & bit) != 0;
		if (!down) {
			_padHeld &= ~bit;
			return false;
		}
		if (was && !fresh) return false;  // a repeat of a held button
		_padHeld |= bit;
		_padDownT[static_cast<std::size_t>(idx)] = t;
		const std::uint32_t m = _c.padMask;
		if (m == 0 || !(m & bit) || (_padHeld & m) != m) return false;
		double first = t;
		for (int i = 0; i < 32; ++i)
			if ((m >> i) & 1u) first = std::min(first, _padDownT[static_cast<std::size_t>(i)]);
		if (t - first > _c.padWindow) return false;
		return Fire(t);
	}
}
