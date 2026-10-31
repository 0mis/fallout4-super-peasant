// OWNER: R6 BASELINE (new file).  See bookmark_io.h.
#include "perf/cost_probe.h"
#include "fo4/bookmark_io.h"

#include "core/log_spool.h"  // s12 fix round 2 lane 3 (log.deferWrites)

#include <REX/W32/KERNEL32.h>
#include <REX/W32/OLE32.h>
#include <REX/W32/SHELL32.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

// s12 fix round 2 lane 3 (log.deferWrites): the log writer's file write (REX::W32 has no WriteFile; as bb_io.cpp declares its own)
extern "C" __declspec(dllimport) int __stdcall WriteFile(void* a_file, const void* a_buf, unsigned long a_n, unsigned long* a_done, void* a_overlapped);

#include <atomic>
#include <chrono>
#include <ctime>
#include <deque>
#include <exception>
#include <format>
#include <fstream>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

namespace sp::fo4::bookmark_io
{
	namespace
	{
		// ---- the log tap: the last kTailLines plugin log lines (time-stamped), any thread (spdlog holds the sink mutex) ----
		constexpr std::size_t kTailLines = 6000;

		struct TailLine
		{
			std::chrono::system_clock::time_point t;
			char                                  level{ 'I' };
			std::string                           text;
		};

		class TailSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			void Copy(std::chrono::system_clock::time_point a_since, std::vector<std::string>& a_out)
			{
				std::scoped_lock l{ this->mutex_ };
				for (const auto& ln : _lines) {
					if (ln.t < a_since) continue;
					const auto tt = std::chrono::system_clock::to_time_t(ln.t);
					std::tm    tm{};
					localtime_s(&tm, &tt);
					const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ln.t.time_since_epoch()).count() % 1000;
					a_out.push_back(std::format("{:02}:{:02}:{:02}.{:03} [{}] {}", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms), ln.level, ln.text));
				}
			}

		protected:
			void sink_it_(const spdlog::details::log_msg& a_msg) override
			{
				TailLine ln;
				ln.t = a_msg.time;
				switch (a_msg.level) {
				case spdlog::level::warn: ln.level = 'W'; break;
				case spdlog::level::err:
				case spdlog::level::critical: ln.level = 'E'; break;
				case spdlog::level::debug:
				case spdlog::level::trace: ln.level = 'D'; break;
				default: ln.level = 'I'; break;
				}
				ln.text.assign(a_msg.payload.data(), a_msg.payload.size());
				_lines.push_back(std::move(ln));
				while (_lines.size() > kTailLines) _lines.pop_front();
			}
			void flush_() override {}

		private:
			std::deque<TailLine> _lines;
		};

		std::shared_ptr<TailSink> g_tail;
		std::once_flag            g_tailOnce;

		// ---- the writer ----
		std::atomic<bool> g_busy{ false };
		std::mutex        g_resLock;
		bool              g_haveRes{ false };
		Result            g_res;

		std::filesystem::path g_folder;
		std::once_flag        g_folderOnce;

		void Finish(Result&& a_r)
		{
			{
				std::scoped_lock l{ g_resLock };
				g_res = std::move(a_r);
				g_haveRes = true;
			}
			g_busy.store(false, std::memory_order_release);
		}

		void WriteJob(std::shared_ptr<sp::bookmark::Capture> a_cap, std::filesystem::path a_dir, std::string a_name)
		{
			SP_COST_SCOPE(Background);
			SP_COST_PARAMETER_TAIL(a_cap);
			SP_COST_PARAMETER_TAIL(a_dir);
			SP_COST_PARAMETER_TAIL(a_name);
			Result     r;
			const auto t0 = std::chrono::steady_clock::now();
			try {
				const std::string text = sp::bookmark::Format(*a_cap);
				std::error_code   ec;
				std::filesystem::create_directories(a_dir, ec);
				if (ec) throw std::runtime_error(std::format("cannot create the folder ({})", ec.message()));
				const auto finalPath = a_dir / a_name;
				auto       tmpPath = finalPath;
				tmpPath += L".tmp";
				{
					std::ofstream f(tmpPath, std::ios::binary | std::ios::trunc);
					if (!f) throw std::runtime_error("cannot open the file for writing");
					f.write(text.data(), static_cast<std::streamsize>(text.size()));
					f.flush();
					if (!f) throw std::runtime_error("write failed (disk full?)");
				}
				std::filesystem::rename(tmpPath, finalPath, ec);
				if (ec) throw std::runtime_error(std::format("cannot rename the file ({})", ec.message()));
				r.ok = true;
				const auto u8 = finalPath.u8string();  // never throws on a non-ANSI user name (string() may)
				r.path.assign(u8.begin(), u8.end());
				r.bytes = text.size();
			} catch (const std::exception& e) {
				r.ok = false;
				r.path = e.what();
			} catch (...) {
				r.ok = false;
				r.path = "unknown error";
			}
			r.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			Finish(std::move(r));
		}
	}

	void InstallLogTap()
	{
		std::call_once(g_tailOnce, [] {
			try {
				auto* lg = spdlog::default_logger_raw();
				if (!lg) return;
				g_tail = std::make_shared<TailSink>();
				g_tail->set_level(spdlog::level::trace);
				lg->sinks().push_back(g_tail);  // plugin load: no other thread logs yet
			} catch (...) {
				g_tail.reset();
			}
		});
	}

	void LogTail(double a_seconds, std::vector<std::string>& a_out)
	{
		if (!g_tail) return;
		try {
			const auto since = std::chrono::system_clock::now() - std::chrono::milliseconds(static_cast<std::int64_t>(std::max(0.0, a_seconds) * 1000.0));
			g_tail->Copy(since, a_out);
		} catch (...) {
		}
	}

	// ---- s12 fix round 2 lane 3: the deferred log write (log.deferWrites; core/log_spool.h) ----
	namespace
	{
		constexpr std::uint32_t kFileAppendData = 0x0004;  // FILE_APPEND_DATA: every write lands at the end of the file
		constexpr std::uint32_t kFileAttrNormal = 0x0080;
		constexpr auto          kWriteEvery = std::chrono::milliseconds(25);
		constexpr double        kSlowWriteMs = 50.0;  // a write this slow is noted in the log (it would have stalled a game frame before)

		struct DeferState
		{
			sp::logspool::Spool        spool;
			void*                      file{ nullptr };  // append-only handle; never closed (the OS closes it at exit)
			std::atomic<std::uint64_t> writes{ 0 }, slow{ 0 }, failed{ 0 }, maxUs{ 0 };
		};
		std::shared_ptr<DeferState> g_defer;  // never released: the writer thread holds it too

		bool WriteAll(void* a_file, const std::string& a_s) noexcept
		{
			const char* p = a_s.data();
			std::size_t left = a_s.size();
			while (left > 0) {
				unsigned long done = 0;
				const auto    n = static_cast<unsigned long>(std::min<std::size_t>(left, std::size_t{ 1 } << 24));
				if (!WriteFile(a_file, p, n, &done, nullptr) || done == 0) return false;
				p += done;
				left -= done;
			}
			return true;
		}
		std::string Stamp(char a_level, std::string_view a_text)
		{
			const auto now = std::chrono::system_clock::now();
			const auto tt = std::chrono::system_clock::to_time_t(now);
			std::tm    tm{};
			localtime_s(&tm, &tt);
			const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
			return std::format("[{:02}:{:02}:{:02}.{:03}] [{:^5}] [{}] {}\n", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms), REX::W32::GetCurrentThreadId(), a_level, a_text);  // (the log pattern: analyzers read the thread id as a number)
		}
		void WriterMain(std::shared_ptr<DeferState> a_st)
		{
			try {
				sp::logspool::WriterLoop(
					a_st->spool,
					[&](const std::string& a_chunk) {
						const auto t0 = std::chrono::steady_clock::now();
						const bool ok = WriteAll(a_st->file, a_chunk);
						const auto us = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count());
						a_st->writes.fetch_add(1, std::memory_order_relaxed);
						if (us > a_st->maxUs.load(std::memory_order_relaxed)) a_st->maxUs.store(us, std::memory_order_relaxed);
						if (!ok) a_st->failed.fetch_add(1, std::memory_order_relaxed);
						if (static_cast<double>(us) / 1000.0 >= kSlowWriteMs) {
							const auto n = a_st->slow.fetch_add(1, std::memory_order_relaxed) + 1;
							if (n <= 50 || n % 100 == 0)  // (the evidence: before log.deferWrites a game frame stood still this long)
								a_st->spool.Append(Stamp('I', std::format("SPN|LOG|slow write ms={:.1f} bytes={} ok={} n={} (on the log writer thread, not a game thread: log.deferWrites)",
																   static_cast<double>(us) / 1000.0, a_chunk.size(), ok, n)));
						}
					},
					[](std::uint64_t a_dropped) { return Stamp('W', std::format("SPN|LOG|dropped {} lines (the deferred log spool was full)", a_dropped)); }, kWriteEvery);
			} catch (...) {
			}
		}
		// spdlog side: format the line as the file sink did and spool it (a warning / error line wakes the writer at once)
		class DeferredFileSink final : public spdlog::sinks::base_sink<std::mutex>
		{
		public:
			explicit DeferredFileSink(std::shared_ptr<DeferState> a_st) : _st(std::move(a_st)) {}

		protected:
			void sink_it_(const spdlog::details::log_msg& a_msg) override
			{
				spdlog::memory_buf_t buf;
				this->formatter_->format(a_msg, buf);
				_st->spool.Append(std::string_view(buf.data(), buf.size()), a_msg.level >= spdlog::level::warn);
			}
			void flush_() override {}  // the writer writes every kWriteEvery (flush_on(info) no longer costs a game frame a disk write)

		private:
			std::shared_ptr<DeferState> _st;
		};
		// DLL detach at process exit (the writer thread is gone): write what is still spooled, without waiting on any lock
		struct ExitFlush
		{
			~ExitFlush()
			{
				try {
					if (auto st = g_defer; st && st->file) {
						std::string rest;
						if (st->spool.TryTakeNow(rest) && !rest.empty()) WriteAll(st->file, rest);
					}
				} catch (...) {
				}
			}
		};
		ExitFlush g_exitFlush;
	}

	bool DeferLogWrites(bool a_on)
	{
		if (!a_on) return false;
		static std::once_flag s_once;
		static bool           s_ok = false;
		std::call_once(s_once, [] {
			try {
				auto* lg = spdlog::default_logger_raw();
				if (!lg) return;
				for (auto& sk : lg->sinks()) {
					auto fs = std::dynamic_pointer_cast<spdlog::sinks::basic_file_sink_mt>(sk);
					if (!fs) continue;
					const std::filesystem::path path(fs->filename());
					fs->flush();
					auto st = std::make_shared<DeferState>();
					st->file = REX::W32::CreateFileW(path.c_str(), kFileAppendData, REX::W32::FILE_SHARE_READ | REX::W32::FILE_SHARE_WRITE | REX::W32::FILE_SHARE_DELETE, nullptr,
						REX::W32::OPEN_ALWAYS, kFileAttrNormal, nullptr);
					if (!st->file || st->file == REX::W32::INVALID_HANDLE_VALUE) return;  // (kept synchronous)
					auto sink = std::make_shared<DeferredFileSink>(st);
					sink->set_level(fs->level());
					sink->set_pattern("[%T.%e] [%=5t] [%L] %v");  // (CommonLibF4's API::InitLog pattern)
					std::thread(WriterMain, st).detach();
					g_defer = st;
					sk = sink;  // plugin load: no other thread logs yet; the old sink is released (its file closes)
					s_ok = true;
					return;
				}
			} catch (...) {
			}
		});
		return s_ok;
	}
	std::string DeferStats()
	{
		auto st = g_defer;
		if (!st) return "off";
		const auto s = st->spool.GetStats();
		return std::format("lines={} dropped={} peakKB={} writes={} slow={} failed={} maxWriteMs={:.1f}", s.lines, s.dropped, s.peakBytes / 1024, st->writes.load(), st->slow.load(),
			st->failed.load(), static_cast<double>(st->maxUs.load()) / 1000.0);
	}

	std::filesystem::path Folder()
	{
		std::call_once(g_folderOnce, [] {
			try {
				wchar_t*   buf = nullptr;
				const auto hr = REX::W32::SHGetKnownFolderPath(REX::W32::FOLDERID_Documents, REX::W32::KF_FLAG_DEFAULT, nullptr, &buf);
				std::unique_ptr<wchar_t[], decltype(&REX::W32::CoTaskMemFree)> owned(buf, REX::W32::CoTaskMemFree);
				if (hr != 0 || !owned) return;
				std::filesystem::path p = owned.get();
				std::string           save(F4SE::GetSaveFolderName());
				if (save.empty()) save = "Fallout4";
				p /= "My Games";
				p /= save;
				p /= "F4SE";
				p /= "SuperPeasant_Bookmarks";
				g_folder = p;
			} catch (...) {
				g_folder.clear();
			}
		});
		return g_folder;
	}

	bool Busy() { return g_busy.load(std::memory_order_acquire); }

	bool Write(sp::bookmark::Capture&& a_cap, const std::string& a_fileName)
	{
		bool expected = false;
		if (!g_busy.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
		const auto dir = Folder();
		if (dir.empty()) {
			Finish({ false, "the Documents folder was not found", 0, 0 });
			return true;
		}
		try {
			auto cap = std::make_shared<sp::bookmark::Capture>(std::move(a_cap));
			std::thread(WriteJob, std::move(cap), dir, a_fileName).detach();
		} catch (...) {
			Finish({ false, "could not start the writer thread", 0, 0 });
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
