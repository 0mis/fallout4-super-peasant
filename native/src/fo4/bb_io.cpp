// OWNER: R6 (round 6 step 2). See bb_io.h.
#include "perf/cost_probe.h"
#include "fo4/bb_io.h"

#include "fo4/bookmark_io.h"

#include <REX/W32/KERNEL32.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// kernel32 (always linked): the writer runs below the game's threads
extern "C" __declspec(dllimport) int __stdcall SetThreadPriority(void* a_thread, int a_priority);
extern "C" __declspec(dllimport) void* __stdcall GetCurrentThread();

namespace sp::fo4::bb_io
{
	namespace
	{
		std::atomic<bool> g_busy{ false };
		std::mutex        g_resLock;
		Result            g_res;
		bool              g_haveRes{ false };
		std::atomic<std::uint32_t> g_seq{ 0 };
		bool              g_seqInit{ false };

		// Removes only the temporary output exclusively created by this job. A failed dump must not
		// retain partial bytes outside List()'s completed-dump retention or affect any older dump.
		struct TempOutput
		{
			const std::filesystem::path& path;
			bool                        owned{ false };
			~TempOutput()
			{
				if (!owned) return;
				std::error_code ignored;
				std::filesystem::remove(path, ignored);
			}
		};

		void Finish(Result&& r)
		{
			{
				std::scoped_lock l{ g_resLock };
				g_res = std::move(r);
				g_haveRes = true;
			}
			g_busy.store(false, std::memory_order_release);
		}

		// <Documents>\My Games\<save folder>\F4SE (the folder of SuperPeasantNative.log)
		std::filesystem::path Folder()
		{
			auto p = sp::fo4::bookmark_io::Folder();
			return p.empty() ? p : p.parent_path();
		}

		// existing SuperPeasantNative.bb<N> files (ours), sorted by N
		std::vector<std::pair<std::uint32_t, std::filesystem::path>> List(const std::filesystem::path& a_dir)
		{
			std::vector<std::pair<std::uint32_t, std::filesystem::path>> v;
			std::error_code ec;
			for (const auto& e : std::filesystem::directory_iterator(a_dir, ec)) {
				if (ec) break;
				const auto name = e.path().filename().string();
				constexpr std::string_view pre = "SuperPeasantNative.bb";
				if (name.size() <= pre.size() || name.compare(0, pre.size(), pre) != 0) continue;
				const auto rest = name.substr(pre.size());
				if (rest.empty() || rest.find_first_not_of("0123456789") != std::string::npos) continue;
				v.emplace_back(static_cast<std::uint32_t>(std::stoul(rest)), e.path());
			}
			std::sort(v.begin(), v.end());
			return v;
		}

		void Job(std::shared_ptr<sp::bb::Snapshot> a_s, double a_maxMB, int a_keep)
		{
			SP_COST_SCOPE(Background);
			SP_COST_PARAMETER_TAIL(a_s);
			::SetThreadPriority(::GetCurrentThread(), -2);  // THREAD_PRIORITY_LOWEST
			const auto t0 = std::chrono::steady_clock::now();
			Result     r;
			r.why = a_s->why;
			try {
				const auto dir = Folder();
				if (dir.empty()) {
					r.path = "the Documents folder was not found";
					Finish(std::move(r));
					return;
				}
				// 1. the in-game replay from the keyframes
				const auto rep = sp::bb::ReplaySnapshot(*a_s);
				r.frames = rep.frames;
				r.mismatches = rep.mismatches;
				r.tapeMisses = rep.tapeMisses;
				r.wFrames = rep.wFrames;  // R6 step 4
				r.wMismatches = rep.wMismatches;
				r.wNoKey = rep.wNoKey;
				r.identical = rep.Identical();
				// 2. the file
				std::uint32_t seq = 0;
				{
					auto files = List(dir);
					if (!g_seqInit) {
						g_seqInit = true;
						g_seq.store(files.empty() ? 0u : files.back().first);
					}
					seq = g_seq.fetch_add(1) + 1;
				}
				a_s->seq = seq;
				sp::bb::FileInfo extra;
				extra.inGameReplay = rep.frames ? (rep.Identical() ? 1 : 0) : -1;
				extra.inGameFrames = rep.frames;
				extra.inGameMismatch = rep.mismatches + rep.tapeMisses;
				std::vector<std::uint8_t> bytes;
				const std::size_t         cap = static_cast<std::size_t>(std::clamp(a_maxMB, 0.25, 16.0) * 1048576.0);
				if (!sp::bb::Serialize(*a_s, bytes, cap, &extra)) {
					r.path = "empty black box (no brain frames recorded)";
					Finish(std::move(r));
					return;
				}
				const auto path = dir / std::format("SuperPeasantNative.bb{}", seq);
				const auto tmp = dir / std::format("SuperPeasantNative.bb{}.tmp", seq);
				TempOutput temporary{ tmp };
				{
					std::FILE* f = nullptr;
					if (_wfopen_s(&f, tmp.c_str(), L"wbx") != 0 || !f) {
						r.path = "could not open " + tmp.string();
						Finish(std::move(r));
						return;
					}
					temporary.owned = true;
					// B2 codex review (BB-02): a short write / a failed close (disk full) is a failed dump, never renamed, reported
					// as replayed or counted for retention; the exclusively created temporary is removed on failure.
					const std::size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
					const int         closed = std::fclose(f);
					if (written != bytes.size() || closed != 0) {
						r.path = "write failed (short write or close error, disk full?): " + tmp.string();
						Finish(std::move(r));
						return;
					}
				}
				std::error_code ec;
				std::filesystem::rename(tmp, path, ec);
				if (ec) {
					r.path = "could not rename: " + ec.message();
					Finish(std::move(r));
					return;
				}
				temporary.owned = false;  // rename transferred our output to the completed-dump lifecycle
				// 3. the summary (parsed back from the bytes just written: proves the file reads)
				{
					sp::bb::Parsed pz;
					std::string    err;
					std::string    txt;
					if (sp::bb::Parse(bytes.data(), bytes.size(), pz, &err))
						txt = sp::bb::Summary(pz, 60);
					else
						txt = "parse failed: " + err + "\n";
					txt = std::format("in-game replay from the keyframes: frames={} mismatches={} tapeMisses={} wardenFrames={} wardenMismatches={} wardenNoKey={} identical={}\n", rep.frames,
						rep.mismatches, rep.tapeMisses, rep.wFrames, rep.wMismatches, rep.wNoKey, rep.Identical()) + txt;
					std::FILE* f = nullptr;
					const auto tpath = dir / std::format("SuperPeasantNative.bb{}.txt", seq);
					if (_wfopen_s(&f, tpath.c_str(), L"wb") == 0 && f) {
						std::fwrite(txt.data(), 1, txt.size(), f);
						std::fclose(f);
					}
				}
				// 4. keep the newest a_keep dumps (ours only: SuperPeasantNative.bb<N> + .txt)
				{
					auto files = List(dir);
					const int keep = std::max(1, a_keep);
					for (int i = 0; i + keep < static_cast<int>(files.size()); ++i) {
						std::filesystem::remove(files[i].second, ec);
						std::filesystem::remove(std::filesystem::path(files[i].second.string() + ".txt"), ec);
					}
				}
				r.ok = true;
				r.path = path.string();
				r.bytes = bytes.size();
			} catch (const std::exception& e) {
				r.ok = false;
				r.path = std::string("exception: ") + e.what();
			} catch (...) {
				r.ok = false;
				r.path = "exception";
			}
			r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			Finish(std::move(r));
		}
	}

	bool Busy() { return g_busy.load(std::memory_order_acquire); }

	bool Write(sp::bb::Snapshot&& a_s, double a_maxMB, int a_keep)
	{
		if (a_s.Empty()) return false;
		bool expected = false;
		if (!g_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
		try {
			auto s = std::make_shared<sp::bb::Snapshot>(std::move(a_s));
			std::thread(Job, std::move(s), a_maxMB, a_keep).detach();
		} catch (...) {
			Result r;
			r.path = "could not start the writer thread";
			Finish(std::move(r));
		}
		return true;
	}

	bool Poll(Result& a_out)
	{
		std::scoped_lock l{ g_resLock };
		if (!g_haveRes) return false;
		a_out = std::move(g_res);
		g_haveRes = false;
		return true;
	}
}
