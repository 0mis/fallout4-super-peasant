#include "perf/cost_probe.h"
#if SP_COST_PROBE
#include "config.h"
#include "runtime.h"
#include <REX/W32/KERNEL32.h>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>
#include <string>

namespace sp::costprobe {
    namespace {
        std::uint64_t Qpc() noexcept {
            std::int64_t value{};
            return REX::W32::QueryPerformanceCounter(&value) && value > 0 ? static_cast<std::uint64_t>(value) : 0;
        }
        countcost::Collector g_collector{ Qpc };
        countcost::Windows g_windows;
        std::atomic<bool> g_ready{ false }, g_on{ false };
        std::mutex g_pumpLock;
        const std::uint32_t* g_frame{};
        std::uint64_t g_frequency{}, g_sequence{}, g_version{}, g_invalid{};
        int g_phase{};
        bool g_wasLive{};
        bool ResolveCounter(std::uint32_t runtime, const std::uint32_t*& out) noexcept {
            // Optional measurement must never call IDDB::offset/get_id: those
            // APIs can terminate the process for a missing ID. The RVA is the
            // already checked trails datum, allowed only on this exact runtime.
            if (runtime != countcost::kRuntime) return false;
            __try {
                const auto module = REX::W32::GetModuleHandleA(nullptr);
                const auto base = reinterpret_cast<std::uintptr_t>(module);
                if (!base) return false;
                const auto* dos = reinterpret_cast<const REX::W32::IMAGE_DOS_HEADER*>(base);
                if (dos->magic != 0x5A4D || dos->lfanew < 0x40 || dos->lfanew > 0x100000) return false;
                const auto* nt = reinterpret_cast<const REX::W32::IMAGE_NT_HEADERS64*>(base + static_cast<std::uint32_t>(dos->lfanew));
                if (nt->signature != 0x4550 || nt->fileHeader.machine != 0x8664 || nt->optionalHeader.magic != 0x20B ||
                    nt->fileHeader.optionalHeaderSize < sizeof(REX::W32::IMAGE_OPTIONAL_HEADER64) ||
                    nt->fileHeader.sectionCount == 0 || nt->fileHeader.sectionCount > 96) return false;
                const auto* sections = REX::W32::IMAGE_FIRST_SECTION(nt);
                bool sectionOk = false;
                for (unsigned i = 0; i < nt->fileHeader.sectionCount; ++i) {
                    if (countcost::CounterSectionValid(runtime, nt->optionalHeader.imageSize, sections[i].virtualAddress,
                                                      sections[i].virtualSize, sections[i].characteristics)) { sectionOk = true; break; }
                }
                if (!sectionOk) return false;
                const auto address = base + countcost::kFrameRva;
                if (address < base) return false;
                REX::W32::MEMORY_BASIC_INFORMATION region{};
                if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(address), &region, sizeof(region)) != sizeof(region) ||
                    region.allocationBase != module || region.type != 0x1000000u || region.state != 0x1000u ||
                    (region.protect & 0x100u) != 0 || ((region.protect & 0xFFu) != 0x04u && (region.protect & 0xFFu) != 0x08u)) return false;
                const auto regionStart = reinterpret_cast<std::uintptr_t>(region.baseAddress);
                if (address < regionStart || region.regionSize < sizeof(std::uint32_t) ||
                    address - regionStart > region.regionSize - sizeof(std::uint32_t)) return false;
                out = reinterpret_cast<const std::uint32_t*>(address);
                return true;
            } __except (1) { return false; }
        }
        bool Sample(countcost::Stamp& out) noexcept {
            __try {
                if (!g_frame) return false;
                out.frame = *g_frame;
                out.qpc = Qpc();
                return out.qpc != 0;
            } __except (1) { return false; }
        }
    }
    countcost::Collector* Active() noexcept { return g_on.load(std::memory_order_relaxed) ? &g_collector : nullptr; }
    void Note(unsigned which) noexcept { if (auto* c = Active()) c->Note(which); }
    void Discontinuity() noexcept { if (g_ready.load(std::memory_order_acquire)) g_collector.Discontinuity(); }
    void Initialize(std::uint32_t runtimeVersion) noexcept {
        try {
            std::int64_t frequency{};
            if (!ResolveCounter(runtimeVersion, g_frame) || !REX::W32::QueryPerformanceFrequency(&frequency) || frequency <= 0) {
                REX::WARN("SPN|COST|invalid v=1 reason=counter-address-or-frequency"); return;
            }
            countcost::Stamp stamp{};
            if (!Sample(stamp)) { REX::WARN("SPN|COST|invalid v=1 reason=counter-read"); return; }
            g_frequency = static_cast<std::uint64_t>(frequency);
            g_ready.store(true, std::memory_order_release);
            g_on.store(sp::cfg::Tunable("perf.costOn", 1.0) != 0, std::memory_order_relaxed);
            REX::INFO("SPN|COST|init v=1 policy={} freq={} frameId=2704653 rva=3D87238 resolver=runtime-pe-rva runtime=010B0F00 on={} testOnly=1", countcost::kPolicy, g_frequency, g_on.load());
        } catch (...) { g_on.store(false); }
    }
    void Pump() noexcept {
        if (!g_ready.load(std::memory_order_acquire)) return;
        try {
            std::unique_lock pump{ g_pumpLock, std::try_to_lock };
            if (!pump.owns_lock()) return;
            const auto version = sp::cfg::TunableVersion();
            if (version != g_version) {
                const bool on = sp::cfg::Tunable("perf.costOn", 1.0) != 0;
                const double phaseValue = sp::cfg::Tunable("perf.costPhase", 0.0);
                const int phase = std::isfinite(phaseValue) && phaseValue >= 0 && phaseValue <= 1000000 ? static_cast<int>(phaseValue) : -1;
                if (phase != g_phase || on != g_on.load()) { g_windows.Reset(); g_collector.Discontinuity(); }
                g_phase = phase; g_on.store(on); g_version = version;
            }
            auto* main = RE::Main::GetSingleton();
            auto* timer = RE::BSTimer::GetSingleton();
            const bool live = g_on.load() && g_phase >= 0 && main && timer && timer->delta > 0 &&
                              !main->inMenuMode && !main->freezeTime && !sp::runtime::IsLoading();
            if (!live) {
                if (g_wasLive) { g_windows.Reset(); g_collector.Discontinuity(); REX::INFO("SPN|COST|gap v=1 seq={} phase={} reason=not-live", g_sequence, g_phase); }
                g_wasLive = false; return;
            }
            g_wasLive = true;
            countcost::Snapshot snapshot{};
            if (!g_collector.TrySnapshot(Sample, snapshot)) {
                if (const auto faults = g_collector.Faults()) {
                    g_on.store(false); g_ready.store(false);
                    REX::WARN("SPN|COST|invalid v=1 reason=snapshot-fault faults={}", faults);
                }
                return;
            }
            countcost::Window window{};
            const auto result = g_windows.Push(snapshot, g_frequency, window);
            if (result == countcost::Result::Gap) {
                REX::INFO("SPN|COST|gap v=1 seq={} phase={} reason=generation discardedFrame0={} discardedQ0={} frame={} qpc={} generation={}",
                          g_sequence, g_phase, window.begin.stamp.frame, window.begin.stamp.qpc,
                          snapshot.stamp.frame, snapshot.stamp.qpc, snapshot.generation);
                return;
            }
            if (result == countcost::Result::Invalid) {
                ++g_invalid;
                g_on.store(false); g_ready.store(false);  // measurement only; never spam every game frame
                REX::WARN("SPN|COST|invalid v=1 seq={} phase={} n={} faults={} frame={} qpc={}", g_sequence, g_phase, g_invalid, snapshot.faults, snapshot.stamp.frame, snapshot.stamp.qpc);
                return;
            }
            if (result == countcost::Result::Start) {
                REX::INFO("SPN|COST|start v=1 next={} phase={} frame={} qpc={} generation={}", g_sequence + 1, g_phase, snapshot.stamp.frame, snapshot.stamp.qpc, snapshot.generation);
                return;
            }
            if (result != countcost::Result::Ready) return;
            std::string categories;
            for (unsigned i = 0; i < countcost::kCategories; ++i) {
                const auto& a = window.begin.categories[i]; const auto& b = window.end.categories[i];
                categories += std::format(" {}={}/{}/{}/{}", countcost::kNames[i], b.calls-a.calls, b.fragments-a.fragments, b.ticks-a.ticks, b.nested-a.nested);
            }
            REX::INFO("SPN|COST|window v=1 seq={} phase={} frame0={} frame1={} frames={} q0={} q1={} freq={} total={} generation={} faults={} invalid={} busy={} high={} liveP5={} acceptedP6={} playerP7={}{}",
                      ++g_sequence, g_phase, window.begin.stamp.frame, window.end.stamp.frame, window.frames,
                      window.begin.stamp.qpc, window.end.stamp.qpc, g_frequency, window.total, snapshot.generation,
                      snapshot.faults, g_invalid, window.end.busy-window.begin.busy, snapshot.highWater,
                      window.end.liveP5-window.begin.liveP5, window.end.acceptedP6-window.begin.acceptedP6,
                      window.end.playerP7-window.begin.playerP7, categories);
        } catch (...) {
            g_collector.Fault(); g_on.store(false); g_ready.store(false);
            try { REX::WARN("SPN|COST|invalid v=1 reason=reporter-exception"); } catch (...) {}
        }
    }
}
#endif
