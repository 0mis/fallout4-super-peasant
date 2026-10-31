// OWNER: NATIVES (C247).  See threads.h.
#include "fo4/threads.h"

#include <array>
#include <atomic>
#include <format>

#include <REX/W32/KERNEL32.h>

namespace sp::fo4::threads
{
	namespace
	{
		thread_local int tl_pump = 0;  // depth of P5 on this thread
		thread_local int tl_peasant = 0;
		thread_local int tl_player = 0;  // CARRY

		std::atomic<std::uint32_t> g_loadTid{ 0 };
		std::atomic<std::uint32_t> g_mainTid{ 0 };    // Main::threadID
		std::atomic<std::uint32_t> g_wndTid{ 0 };     // window thread of Main::hwnd
		std::atomic<bool>          g_mainChecked{ false };
		std::atomic<std::uint32_t> g_pumpTid{ 0 };    // thread of the pump running now (0 = none)
		std::atomic<std::uint64_t> g_pumps{ 0 }, g_pumpsMain{ 0 };
		std::atomic<std::uint64_t> g_overlap{ 0 }, g_actorUpd{ 0 };
		std::atomic<std::uint64_t> g_waitNat{ 0 }, g_waitNatInPump{ 0 }, g_waitNatMain{ 0 };
		std::atomic<std::uint64_t> g_deferred{ 0 };
		std::atomic<std::uint32_t> g_lastWaitTid{ 0 };
		// distinct pump thread ids (first 16)
		std::array<std::atomic<std::uint32_t>, 16> g_pumpTids{};
		std::atomic<int>                           g_nPumpTids{ 0 };

		std::uint32_t Tid() { return REX::W32::GetCurrentThreadId(); }

		void CheckMain()
		{
			if (g_mainChecked.load(std::memory_order_acquire)) return;
			auto* m = RE::Main::GetSingleton();
			if (!m) return;
			const std::uint32_t mt = m->threadID;
			// user32!GetWindowThreadProcessId, resolved at run time (no new link dependency)
			using GWTPI = unsigned long(__stdcall*)(void*, unsigned long*);
			static const auto gwtpi = reinterpret_cast<GWTPI>(REX::W32::GetProcAddress(REX::W32::GetModuleHandleW(L"user32.dll"), "GetWindowThreadProcessId"));
			unsigned long       pid = 0;
			const std::uint32_t wt = (m->hwnd && gwtpi) ? static_cast<std::uint32_t>(gwtpi(reinterpret_cast<void*>(m->hwnd), &pid)) : 0u;
			g_mainTid.store(mt);
			g_wndTid.store(wt);
			g_mainChecked.store(true, std::memory_order_release);
			REX::INFO("SPN|THREAD|main thread: Main::threadID {} window thread {} plugin-load thread {} -> {} (pump thread now {})", mt, wt, g_loadTid.load(),
				mt && (mt == wt || !wt) ? "Main::threadID confirmed" : "MISMATCH (Main::threadID offset suspect; window thread used)", Tid());
			if (mt && wt && mt != wt) g_mainTid.store(wt);
		}

		void RememberPumpTid(std::uint32_t t)
		{
			const int n = g_nPumpTids.load(std::memory_order_relaxed);
			for (int i = 0; i < n && i < 16; ++i)
				if (g_pumpTids[i].load(std::memory_order_relaxed) == t) return;
			if (n < 16) {
				g_pumpTids[n].store(t, std::memory_order_relaxed);
				g_nPumpTids.store(n + 1, std::memory_order_relaxed);
			}
		}
	}

	void OnPluginLoad() { g_loadTid.store(Tid()); }

	void OnPumpBegin()
	{
		if (tl_pump++ > 0) return;
		const auto t = Tid();
		g_pumpTid.store(t, std::memory_order_release);
		CheckMain();
		g_pumps.fetch_add(1, std::memory_order_relaxed);
		if (t == g_mainTid.load(std::memory_order_relaxed)) g_pumpsMain.fetch_add(1, std::memory_order_relaxed);
		RememberPumpTid(t);
	}

	void OnPumpEnd()
	{
		if (--tl_pump > 0) return;
		if (tl_pump < 0) tl_pump = 0;
		g_pumpTid.store(0, std::memory_order_release);
	}

	bool          InPump() { return tl_pump > 0; }
	std::uint32_t MainTid() { return g_mainTid.load(std::memory_order_relaxed); }
	bool          IsMainThread()
	{
		const auto m = MainTid();
		return m != 0 && Tid() == m;
	}

	void OnPeasantUpdateBegin() { ++tl_peasant; }
	void OnPeasantUpdateEnd()
	{
		if (tl_peasant > 0) --tl_peasant;
	}
	bool InPeasantUpdate() { return tl_peasant > 0; }

	void OnPlayerUpdateBegin() { ++tl_player; }  // CARRY
	void OnPlayerUpdateEnd()
	{
		if (tl_player > 0) --tl_player;
	}
	bool InPlayerUpdate() { return tl_player > 0; }

	bool SafeForEngineWrites() { return InPump() || (g_pumps.load(std::memory_order_relaxed) == 0 && IsMainThread()); }

	void NoteActorUpdate()
	{
		g_actorUpd.fetch_add(1, std::memory_order_relaxed);
		const auto p = g_pumpTid.load(std::memory_order_relaxed);
		if (p != 0 && tl_pump == 0) g_overlap.fetch_add(1, std::memory_order_relaxed);
	}

	void NoteWaitNative()
	{
		g_waitNat.fetch_add(1, std::memory_order_relaxed);
		const auto t = Tid();
		g_lastWaitTid.store(t, std::memory_order_relaxed);
		if (tl_pump > 0) g_waitNatInPump.fetch_add(1, std::memory_order_relaxed);
		if (t == MainTid()) g_waitNatMain.fetch_add(1, std::memory_order_relaxed);
	}

	void NoteDeferred(const char* a_what)
	{
		const auto n = g_deferred.fetch_add(1, std::memory_order_relaxed) + 1;
		if (n <= 10) REX::WARN("SPN|W|engine write '{}' requested off the safe point (thread {}) -> deferred to the F4SE task pump", a_what ? a_what : "?", Tid());
	}

	std::string Describe()
	{
		std::string ids;
		const int   n = g_nPumpTids.load(std::memory_order_relaxed);
		for (int i = 0; i < n && i < 16; ++i) ids += std::format("{}{}", i ? "," : "", g_pumpTids[i].load(std::memory_order_relaxed));
		return std::format("main={} wnd={} load={} pumps={} onMain={} pumpThreads={}[{}] actorUpd={} overlapPump={} waitNat={} (inPump {} onMain {} last t{}) deferred={}", g_mainTid.load(),
			g_wndTid.load(), g_loadTid.load(), g_pumps.load(), g_pumpsMain.load(), n, ids, g_actorUpd.load(), g_overlap.load(), g_waitNat.load(), g_waitNatInPump.load(), g_waitNatMain.load(),
			g_lastWaitTid.load(), g_deferred.load());
	}
}
