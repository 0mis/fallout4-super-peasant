// R6 BASELINE: the "that was weird" BOOKMARK key (core/bookmark.h). Desktop cases:
//   BK-1 the ring keeps the newest entries in order, never allocates on Push, CopySince cuts by time
//   BK-2 Recorder::Grab copies exactly the last N s of frames / events / candidate rows
//   BK-3 MakeEvent copies and truncates its text (no dangling pointers in a capture)
//   BK-4 Summarize: picks / switches / drops, per-target time and swings / hits, idle while a foe attacked the player,
//        player health, fps and cost
//   BK-5 Format: every section, one CSV row per frame, times are seconds before the press, null reasons are safe, an
//        empty capture formats
//   BK-6 cost: one frame record per game frame costs well under the 0.05 ms budget (measured), and a 10 s capture at
//        144 fps formats in a few ms (off the game thread in game)
//   BK-7 keyboard trigger: the press edge fires once (held repeats do not), the modifier, the debounce, key 0 = off
//   BK-8 controller chord: View + Y in either order within the window fires; too slow, one button alone, a repeat or
//        mask 0 does not; the debounce
//   BK-9 names: file name, key names, pad names
//   BK-10 the shipped ini: the bookmark block at its defaults, no TEST-only bookmark key, the key is not a Fallout 4 /
//        "Carry me" key, the required [Tabs] keys kept
#include "core/bookmark.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace bookmark_tests
{
	namespace B = sp::bookmark;

	inline B::Frame MakeFrame(double t, std::uint64_t n, std::uint32_t target, bool vis, float dt = 1.0f / 120.0f)
	{
		B::Frame f;
		f.t = t;
		f.frame = n;
		f.dt = dt;
		f.costMs = 0.2f;
		f.mode = 3;
		f.flags = B::kFBrainOn | (vis ? B::kFVisible : 0u);
		f.target = target;
		f.dist = 300.0f;
		f.stageWhy = "blinkWait";
		f.playerHp = 0.8f;
		return f;
	}

	inline void Run()
	{
		// ---- BK-1 ring ----
		{
			B::Ring<B::Frame> r;
			r.Reserve(8);
			for (int i = 0; i < 20; ++i) r.Push(MakeFrame(i * 0.1, static_cast<std::uint64_t>(i), 0, false));
			CHECK(r.Size() == 8 && r.Capacity() == 8, "BK-1 ring keeps capacity entries (%zu / %zu)", r.Size(), r.Capacity());
			bool order = true;
			for (std::size_t i = 0; i < r.Size(); ++i) order = order && r.At(i).frame == 12 + i;
			CHECK(order, "BK-1 ring keeps the newest entries oldest first");
			std::vector<B::Frame> out;
			r.CopySince(1.55, out);
			CHECK(out.size() == 4 && out.front().frame == 16 && out.back().frame == 19, "BK-1 CopySince cuts by time (%zu, first %llu)", out.size(),
				out.empty() ? 0ull : static_cast<unsigned long long>(out.front().frame));
			r.Push(MakeFrame(9.0, 99, 0, false));
			CHECK(r.Size() == 8 && r.At(7).frame == 99 && r.At(0).frame == 13, "BK-1 Push overwrites the oldest");
		}

		// ---- BK-2 recorder window ----
		{
			B::Recorder rec;
			rec.Configure(4096, 4096, 8192);
			for (int i = 0; i < 120 * 30; ++i) {
				const double t = i / 120.0;
				rec.Push(MakeFrame(t, static_cast<std::uint64_t>(i), 0x100, true));
				if (i % 12 == 0) rec.Push(B::MakeEvent(t, static_cast<std::uint64_t>(i), B::Ev::kSwing, 0x100));
				if (i % 30 == 0) {
					B::Cand c;
					c.t = t;
					c.id = 0x100;
					rec.Push(c);
				}
			}
			B::Capture cap;
			const double now = (120 * 30 - 1) / 120.0;
			rec.Grab(now, 10.0, cap);
			const double span = cap.frames.empty() ? 0 : cap.frames.back().t - cap.frames.front().t;
			CHECK(cap.frames.size() >= 1199 && cap.frames.size() <= 1201 && span <= 10.0 && span > 9.9, "BK-2 10 s at 120 fps = ~1200 frames (%zu, span %.3f)", cap.frames.size(), span);
			CHECK(cap.events.size() >= 99 && cap.events.size() <= 101, "BK-2 events of the window only (%zu)", cap.events.size());
			CHECK(cap.cands.size() >= 39 && cap.cands.size() <= 41, "BK-2 candidate rows of the window only (%zu)", cap.cands.size());
			CHECK(cap.tPress == now && cap.window == 10.0, "BK-2 the capture knows its press time and window");
			rec.Clear();
			B::Capture cap2;
			rec.Grab(now, 10.0, cap2);
			CHECK(cap2.frames.empty() && cap2.events.empty() && cap2.cands.empty() && rec.Configured(), "BK-2 Clear empties the rings, keeps the buffers");
		}

		// ---- BK-3 event text ----
		{
			std::string long_(200, 'x');
			char        tmp[32];
			std::strcpy(tmp, "temporary text");
			const B::Event e = B::MakeEvent(1.0, 5, B::Ev::kVeto, 0x14, 0, 0, 0.0f, tmp);
			std::strcpy(tmp, "OVERWRITTEN!!!");
			CHECK(std::string(e.text) == "temporary text", "BK-3 the event keeps its own copy of the text");
			const B::Event e2 = B::MakeEvent(1.0, 5, B::Ev::kNote, 0, 0, 0, 0.0f, long_.c_str());
			CHECK(std::strlen(e2.text) == sizeof(e2.text) - 1, "BK-3 long text is truncated (%zu)", std::strlen(e2.text));
			const B::Event e3 = B::MakeEvent(1.0, 5, B::Ev::kNote);
			CHECK(e3.text[0] == 0, "BK-3 no text = empty");
		}

		// ---- BK-4 summary ----
		{
			B::Capture c;
			c.tPress = 10.0;
			c.window = 10.0;
			// 0..2 s no target while an attacker is in the table; 2..6 s target A (visible); 6..8 s target B; 8..10 s none
			int n = 0;
			for (double t = 0.0; t < 10.0 - 1e-9; t += 0.01) {
				std::uint32_t tgt = t < 2.0 ? 0u : t < 6.0 ? 0xA0u : t < 8.0 ? 0xB0u : 0u;
				auto          f = MakeFrame(t, static_cast<std::uint64_t>(n++), tgt, tgt == 0xA0u, 0.01f);
				f.playerHp = static_cast<float>(1.0 - t / 20.0);
				if (t > 7.0 && t < 7.5) f.blockedFor = static_cast<float>(t - 7.0);
				c.frames.push_back(f);
			}
			for (double t = 0.0; t < 2.0; t += 0.25) {
				B::Cand r;
				r.t = t;
				r.id = 0xA0;
				r.flags = B::kCHostile | B::kCAttackingPlayer;
				c.cands.push_back(r);
			}
			for (int i = 0; i < 10; ++i) c.events.push_back(B::MakeEvent(2.0 + i * 0.3, 0, B::Ev::kSwing, 0xA0));
			for (int i = 0; i < 3; ++i) c.events.push_back(B::MakeEvent(2.1 + i * 0.3, 0, B::Ev::kHit, 0xA0, 0, 1, 1.5f, "native"));
			c.events.push_back(B::MakeEvent(6.5, 0, B::Ev::kSwing, 0xB0));
			c.events.push_back(B::MakeEvent(7.0, 0, B::Ev::kGiveUp, 0xB0, 0, 2, 0.0f, "unreach"));
			const auto s = B::Summarize(c);
			CHECK(s.picks == 1 && s.switches == 1 && s.drops == 1, "BK-4 picks / switches / drops (%d / %d / %d)", s.picks, s.switches, s.drops);
			CHECK(s.targets.size() == 2 && s.targets[0].id == 0xA0 && std::abs(s.targets[0].seconds - 4.0) < 0.05 && std::abs(s.targets[0].visibleS - 4.0) < 0.05 &&
			          std::abs(s.targets[1].seconds - 2.0) < 0.05 && s.targets[1].visibleS < 0.01,
				"BK-4 per-target time (A %.2f s vis %.2f, B %.2f s)", s.targets.empty() ? 0 : s.targets[0].seconds, s.targets.empty() ? 0 : s.targets[0].visibleS,
				s.targets.size() > 1 ? s.targets[1].seconds : 0);
			CHECK(s.swings == 11 && s.hits == 3 && s.targets[0].swings == 10 && s.targets[0].hits == 3 && s.targets[1].swings == 1 && s.giveUps == 1,
				"BK-4 swings / hits per target (%d / %d)", s.swings, s.hits);
			CHECK(s.idleS > 1.6 && s.idleS < 2.05, "BK-4 idle while a foe attacked the player = the first 2 s (%.2f)", s.idleS);
			CHECK(std::abs(s.playerHpMin - 0.5) < 0.01 && std::abs(s.playerHpLast - 0.5) < 0.01, "BK-4 player health lowest / last (%.3f / %.3f)", s.playerHpMin, s.playerHpLast);
			CHECK(std::abs(s.fpsAvg - 100.0) < 0.5 && std::abs(s.costAvgMs - 0.2) < 0.01 && s.blockedMaxS > 0.45, "BK-4 fps %.1f cost %.3f blocked %.2f", s.fpsAvg, s.costAvgMs, s.blockedMaxS);
		}

		// ---- BK-5 format ----
		{
			B::Capture c;
			c.tPress = 5.0;
			c.window = 10.0;
			c.when = "2026-10-12 21:04:55.123";
			c.how = "keyboard F8";
			c.header.push_back("plugin     test");
			for (int i = 0; i < 50; ++i) {
				auto f = MakeFrame(4.0 + i * 0.02, static_cast<std::uint64_t>(1000 + i), i < 25 ? 0x1234u : 0x5678u, true, 0.02f);
				if (i == 3) {
					f.stageWhy = nullptr;
					f.down = nullptr;
				}
				if (i == 4) f.down = "knocked";
				if (i == 5) {
					f.giveUp = 2;
					f.giveUpId = 0x1234;
				}
				c.frames.push_back(f);
			}
			c.events.push_back(B::MakeEvent(4.5, 1025, B::Ev::kTarget, 0x5678, 0x1234, 1, 250.0f, "tier=E cls=r vis=1 move=approach | old: blind"));
			c.events.push_back(B::MakeEvent(4.6, 1030, B::Ev::kHit, 0x5678, 1, 0, 1.8f, "native (brain)"));
			B::Cand r;
			r.t = 4.75;
			r.id = 0x5678;
			r.flags = B::kCHostile | B::kCTarget;
			r.whyNot = nullptr;
			c.cands.push_back(r);
			r.id = 0x9999;
			r.flags = B::kCHostile;
			r.whyNot = "far";
			c.cands.push_back(r);
			c.log.push_back("21:04:54.000 [I] SPN|I|brain target 00005678 tier=E");
			const std::string o = B::Format(c);
			const auto has = [&](const char* k) { return o.find(k) != std::string::npos; };
			CHECK(has("== SUMMARY ==") && has("== EVENTS (2) ==") && has("== CANDIDATES") && has("== FRAMES (every frame) ==") && has("== LOG") && has("== END =="),
				"BK-5 every section is there");
			CHECK(has("keyboard F8") && has("2026-10-12 21:04:55.123") && has("plugin     test"), "BK-5 header (time, trigger, lines)");
			std::size_t csv = 0, pos = o.find("me_x,me_y,me_z");
			for (; pos != std::string::npos && (pos = o.find('\n', pos + 1)) != std::string::npos;) {
				if (o.compare(pos + 1, 1, "-") == 0 || o.compare(pos + 1, 1, "+") == 0) ++csv;
				else break;
			}
			CHECK(csv == 50, "BK-5 one CSV row per frame (%zu)", csv);
			CHECK(has("\n-1.0000,1000,") && has("\n-0.0200,1049,"), "BK-5 frame times are seconds before the press");
			CHECK(has(",knocked,") && has("unreach:00001234"), "BK-5 the down reason and the give-up are in the rows");
			CHECK(has("*00005678") && has("why=eligible") && has("why=far") && has(" HIT "), "BK-5 candidate rows (target starred, reasons) and events");
			CHECK(has("00005678  target 0.50 s") && has("00001234  target 0.50 s"), "BK-5 the summary lists both targets");
#pragma warning(suppress : 4996)
			if (const char* sample = std::getenv("SP_BK_SAMPLE"); sample && *sample) {  // a sample file for the docs (not a check)
				if (FILE* fh = std::fopen(sample, "wb")) {
					std::fwrite(o.data(), 1, o.size(), fh);
					std::fclose(fh);
				}
			}
			B::Capture  empty;
			const auto  e = B::Format(empty);
			CHECK(e.find("== END ==") != std::string::npos && e.find("0 frames") != std::string::npos, "BK-5 an empty capture formats");
		}

		// ---- BK-6 cost ----
		{
			B::Recorder rec;
			rec.Configure(4096, 4096, 8192);
			const int N = 200000;
			volatile std::uint32_t sink = 0;
			const auto t0 = std::chrono::steady_clock::now();
			for (int i = 0; i < N; ++i) {
				B::Frame f = MakeFrame(i / 144.0, static_cast<std::uint64_t>(i), static_cast<std::uint32_t>(i & 7), (i & 1) != 0);
				f.me[0] = static_cast<float>(i);
				f.swings = static_cast<std::uint32_t>(i / 10);
				rec.Push(f);
				sink = sink + f.target;
			}
			const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / N;
			std::printf("info: BK-6 one frame record + push: %.3f us (budget 50 us per frame)\n", us);
			CHECK(us < 5.0, "BK-6 a frame record costs %.3f us (< 5 us; the game budget is 50 us)", us);
			for (int i = 0; i < 200; ++i) rec.Push(B::MakeEvent((N - 1) / 144.0 - i * 0.04, 0, B::Ev::kSwing, 7, 0, 0, 0.0f, "x"));
			B::Capture c;
			const auto t1 = std::chrono::steady_clock::now();
			rec.Grab((N - 1) / 144.0, 10.0, c);
			const double grabMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
			const auto   t2 = std::chrono::steady_clock::now();
			const auto   text = B::Format(c);
			const double fmtMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t2).count();
			std::printf("info: BK-6 10 s at 144 fps: grab %.3f ms (game thread, once per press), format %.1f ms (writer thread), %zu bytes\n", grabMs, fmtMs, text.size());
			CHECK(grabMs < 5.0 && c.frames.size() >= 1439 && c.frames.size() <= 1441, "BK-6 the press copies %zu frames in %.3f ms", c.frames.size(), grabMs);
			CHECK(text.size() < 4u * 1024u * 1024u, "BK-6 a 10 s bookmark stays small (%zu bytes)", text.size());
			(void)sink;
		}

		// ---- BK-7 keyboard trigger ----
		{
			B::Trigger tr;
			B::TriggerCfg cfg;  // F8, no modifier, debounce 2 s
			tr.SetCfg(cfg);
			CHECK(!tr.Key(118, true, 0.0) && !tr.Key(118, false, 0.1), "BK-7 another key does nothing");
			CHECK(tr.Key(119, true, 1.0), "BK-7 F8 press fires");
			CHECK(!tr.Key(119, true, 1.1) && !tr.Key(119, true, 1.2), "BK-7 held-key repeats do not fire again");
			tr.Key(119, false, 1.3);
			CHECK(!tr.Key(119, true, 2.0) && tr.Debounced() == 1, "BK-7 a second press within 2 s is one bookmark");
			tr.Key(119, false, 2.1);
			CHECK(tr.Key(119, true, 3.5) && tr.Fired() == 2, "BK-7 after the debounce it fires again");
			tr.Key(119, false, 3.6);
			cfg.keyMod = 160;
			tr.SetCfg(cfg);
			CHECK(!tr.Key(119, true, 10.0), "BK-7 with a modifier set, F8 alone does nothing");
			tr.Key(119, false, 10.1);
			tr.Key(160, true, 10.2);
			CHECK(tr.Key(119, true, 10.3), "BK-7 modifier held + F8 fires");
			tr.Key(119, false, 10.4);
			tr.Key(160, false, 10.5);
			cfg.key = 0;
			cfg.keyMod = 0;
			tr.SetCfg(cfg);
			CHECK(!tr.Key(119, true, 20.0) && !tr.Key(0, true, 20.1), "BK-7 key 0 = no keyboard key");
			// a lost release (alt-tab while F8 was down): the engine's next fresh press still counts
			B::Trigger tr2;
			tr2.SetCfg(B::TriggerCfg{});
			CHECK(tr2.Key(119, true, 1.0, true), "BK-7 fresh press fires");
			CHECK(!tr2.Key(119, true, 1.5, false), "BK-7 a repeat does not");
			CHECK(tr2.Key(119, true, 5.0, true), "BK-7 a fresh press after a lost release fires");
		}

		// ---- BK-8 controller chord ----
		{
			B::Trigger tr;
			B::TriggerCfg cfg;  // View (0x20) + Y (0x8000) within 0.5 s
			tr.SetCfg(cfg);
			CHECK(!tr.Pad(0x8000, true, 0.0) && !tr.Pad(0x8000, false, 0.1), "BK-8 Y alone (a jump) does nothing");
			CHECK(!tr.Pad(0x20, true, 1.0), "BK-8 View alone does nothing");
			CHECK(tr.Pad(0x8000, true, 1.3), "BK-8 View then Y within 0.5 s fires");
			tr.Pad(0x8000, false, 1.4);
			tr.Pad(0x20, false, 1.5);
			tr.Pad(0x8000, true, 5.0);
			CHECK(tr.Pad(0x20, true, 5.1), "BK-8 Y then View fires too");
			tr.Pad(0x8000, false, 5.2);
			tr.Pad(0x20, false, 5.3);
			tr.Pad(0x20, true, 10.0);
			CHECK(!tr.Pad(0x8000, true, 10.9), "BK-8 too slow (0.9 s apart) does nothing");
			CHECK(!tr.Pad(0x8000, true, 11.0), "BK-8 a repeat of a held button does nothing");
			tr.Pad(0x8000, false, 11.1);
			tr.Pad(0x20, false, 11.2);
			tr.Pad(0x20, true, 12.0);
			CHECK(!tr.Pad(0x1000, true, 12.1), "BK-8 View + A is not the chord");
			tr.Pad(0x1000, false, 12.2);
			tr.Pad(0x20, false, 12.3);
			tr.Pad(0x20, true, 13.0);
			CHECK(tr.Pad(0x8000, true, 13.2), "BK-8 fires again after the debounce");
			tr.Pad(0x20, false, 13.3);
			tr.Pad(0x8000, false, 13.3);
			tr.Pad(0x20, true, 14.0);
			CHECK(!tr.Pad(0x8000, true, 14.1) && tr.Debounced() >= 1, "BK-8 within 2 s of the last one: debounced");
			tr.Pad(0x20, false, 14.2);
			tr.Pad(0x8000, false, 14.2);
			CHECK(!tr.Pad(0x8020, true, 20.0), "BK-8 a multi-bit edge is ignored");
			tr.Pad(0x20, true, 40.0, true);
			CHECK(tr.Pad(0x8000, true, 40.2, true), "BK-8 fresh presses fire");
			// both releases lost: fresh presses 10 s later still make the chord (the old press times do not count)
			tr.Pad(0x20, true, 50.0, true);
			CHECK(tr.Pad(0x8000, true, 50.1, true), "BK-8 after lost releases the next fresh chord fires");
			tr.Pad(0x20, false, 50.2);
			tr.Pad(0x8000, false, 50.2);
			cfg.padMask = 0;
			tr.SetCfg(cfg);
			tr.Pad(0x20, true, 30.0);
			CHECK(!tr.Pad(0x8000, true, 30.1), "BK-8 mask 0 = no controller chord");
		}

		// ---- BK-9 names ----
		{
			CHECK(B::FileName(2026, 10, 12, 21, 4, 5, 7) == "bookmark_20261012_210405_007.log", "BK-9 file name (%s)", B::FileName(2026, 10, 12, 21, 4, 5, 7).c_str());
			CHECK(B::KeyName(119) == "F8" && B::KeyName(121) == "F10" && B::KeyName(71) == "G" && B::KeyName(160) == "LShift", "BK-9 key names");
			CHECK(B::PadName(0x8020) == "View+Y" && B::PadName(0) == "off" && B::PadName(0x300) == "LB+RB", "BK-9 pad names (%s)", B::PadName(0x8020).c_str());
			int named = 0;
			for (int k = 0; k < static_cast<int>(B::Ev::kCount); ++k) named += std::string(B::EvName(static_cast<B::Ev>(k))) != "?";
			CHECK(named == static_cast<int>(B::Ev::kCount), "BK-9 every event kind has a name (%d)", named);
		}

		// ---- BK-10 the shipped ini ----
		{
			std::string path = __FILE__;
			path = path.substr(0, path.find_last_of("/\\")) + "/../SuperPeasantNative.ini";
			static std::string line;
			const bool         ok = sp::cfg::LoadIni(path, [](const std::string& a) { line = a; });
			CHECK(ok && line.find(" 0 ignored") != std::string::npos, "BK-10 the shipped ini parses with 0 ignored keys: %s", line.c_str());
			const auto T = [](const char* k) { return sp::cfg::Tunable(k, -12345.0); };
			CHECK(T("bookmark.on") == 1 && T("bookmark.key") == 119 && T("bookmark.keyMod") == 0 && T("bookmark.pad") == 32800 && T("bookmark.padWindow") == 0.5 &&
			          T("bookmark.seconds") == 10 && T("bookmark.debounce") == 2 && T("bookmark.notify") == 1,
				"BK-10 the bookmark block is at its defaults");
			CHECK(T("debug.bookmarkAt") == -12345.0 && T("debug.bookmarkNow") == -12345.0, "BK-10 no TEST-only bookmark key in the shipped ini");
			// Fallout 4's own keyboard keys (ControlMap: W A S D E R Tab V Space LShift Q LCtrl C CapsLock X F 1-0 - = F5 F9 Esc PrtSc
			// ` Insert F12 K I J M O LAlt) and "Carry me" (G = 71)
			static constexpr int kGameKeys[] = { 0x57, 0x41, 0x53, 0x44, 0x45, 0x52, 0x09, 0x56, 0x20, 0xA0, 0x51, 0xA2, 0x43, 0x14, 0x58, 0x46, 0x31, 0x32, 0x33, 0x34,
				0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x74, 0x78, 0x1B, 0x2C, 0xC0, 0x2D, 0x7B, 0x4B, 0x49, 0x4A, 0x4D, 0x4F, 0xA4, 71 };
			bool clash = false;
			for (int k : kGameKeys) clash = clash || k == static_cast<int>(T("bookmark.key"));
			CHECK(!clash && T("bookmark.key") != T("carry.hotkeyKb"), "BK-10 the bookmark key is not a Fallout 4 key nor the carry key");
			CHECK(sp::cfg::Tunable("fo4.forceConvention", -1) == 1 && sp::cfg::Tunable("brain.range", -1) == 1 && sp::cfg::Tunable("brain.requireCombat", -1) == 0,
				"BK-10 required [Tabs] keys kept");
		}
	}
}
