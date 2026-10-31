#include "perf/count_collector.h"
#include "perf/cost_probe.h"  // OFF must remain engine-independent and preserve targets
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace sp::countcost;
namespace {
    std::atomic<std::uint64_t> now{ 10 };
    std::atomic<bool> perThreadClock{ false };
    std::uint32_t frame = 10;
    std::uint64_t ReadFakeCostClock() noexcept {
        if (perThreadClock.load()) { static thread_local std::uint64_t tick = 100; return ++tick; }
        return now.load();
    }
    bool SampleNow(Stamp& out) noexcept { out = { now.load(), frame }; return true; }
    bool BadSample(Stamp&) noexcept { return false; }
    int identity(int x) { return x; }
    static_assert(std::is_same_v<decltype(&SP_COST_NATIVE(identity)), decltype(&identity)>);
    static_assert(&SP_COST_NATIVE(identity) == &identity);
    unsigned checks{}, failures{};
    void Check(bool value, const char* message) {
        ++checks;
        if (!value) { ++failures; std::cerr << "FAIL " << message << '\n'; }
    }
    const Counts& Count(const Snapshot& s, Category c) { return s.categories[static_cast<unsigned>(c)]; }
    Snapshot Take(Collector& c) { Snapshot s; Check(c.TrySnapshot(SampleNow, s), "quiescent snapshot succeeds"); return s; }
    struct OwnedCleanup { ~OwnedCleanup() noexcept { now = 250; } };
    void OwnedJob(Collector& c, std::unique_ptr<OwnedCleanup> owner) {
        Scope scope{ &c, Category::Background };
        ParameterTail<decltype(owner)> tail{ owner };
        now = 200;
    }
}
void VerifyNativeWrappers();

namespace cost_bench
{
#if defined(_WIN32)
    // Bounded empirical calibration only. The distributions below contain CHUNK
    // MEANS, not per-invocation latency quantiles or hard future upper bounds.
    constexpr unsigned kSerialChunks = 32, kSerialIterations = 2048;
    constexpr unsigned kContendedChunks = 12, kContendedIterations = 1024;
    constexpr double kDeadlineSeconds = 20.0;
    std::atomic<bool> qpcFailed{ false }, gateOn{ false };
    Collector* gatedCollector{};  // published before workers start; changed only after they join
    thread_local volatile std::uint32_t loopMarker{};

    std::uint64_t ReadBenchQpc() noexcept
    {
        LARGE_INTEGER value{};
        if (!::QueryPerformanceCounter(&value) || value.QuadPart <= 0) { qpcFailed.store(true); return 0; }
        return static_cast<std::uint64_t>(value.QuadPart);
    }
    bool SampleBenchQpc(Stamp& out) noexcept { out = { ReadBenchQpc(), 0 }; return out.qpc != 0; }
    // The production Active() is a cross-translation-unit bool gate and pointer
    // selection. Emulate its call here; never claim identical generated code.
    __declspec(noinline) Collector* BenchActive() noexcept
    {
        return gateOn.load(std::memory_order_relaxed) ? gatedCollector : nullptr;
    }
    void MarkLoop() noexcept
    {
        const auto value = loopMarker;  // same observable empty-loop body in both arms
        (void)value;
        std::atomic_signal_fence(std::memory_order_seq_cst);
    }
    enum class Mode { BareRoot, Root, BareNested, Nested };
    struct ThreadRun { std::uint64_t begin{}, end{}; bool clean{}; };
    struct Run {
        std::uint64_t threadTicks{}, wallTicks{}, includedTicks{}, calls{}, fragments{}, nested{}, high{};
        bool valid{};
    };
    Run Measure(Mode mode, unsigned iterations, unsigned threadCount)
    {
        Collector collector{ ReadBenchQpc };
        gatedCollector = &collector;
        gateOn.store(true, std::memory_order_relaxed);
        std::vector<ThreadRun> samples(threadCount);
        std::barrier start(static_cast<std::ptrdiff_t>(threadCount + 1));
        auto work = [&](unsigned t, bool synchronize) {
            if (synchronize) start.arrive_and_wait();
            auto& sample = samples[t];
            sample.begin = ReadBenchQpc();
            switch (mode) {
            case Mode::BareRoot:
                for (unsigned i = 0; i < iterations; ++i) MarkLoop();
                break;
            case Mode::Root:
                for (unsigned i = 0; i < iterations; ++i) {
                    MarkLoop();  // outside the recorded interval: the Scope itself is empty
                    Scope empty{ BenchActive(), Category::P5 };
                }
                break;
            case Mode::BareNested: {
                Scope parent{ BenchActive(), Category::P5 };
                for (unsigned i = 0; i < iterations; ++i) MarkLoop();
                break;
            }
            case Mode::Nested: {
                Scope parent{ BenchActive(), Category::P5 };
                for (unsigned i = 0; i < iterations; ++i) {
                    MarkLoop();
                    Scope child{ BenchActive(), Category::Native };
                }
                break;
            }
            }
            sample.end = ReadBenchQpc();
            sample.clean = tl.depth == 0 && tl.collector == nullptr;
        };
        if (threadCount == 1) work(0, false);
        else {
            std::vector<std::thread> workers;
            workers.reserve(threadCount);
            bool launchFailed = false;
            try {
                for (unsigned t = 0; t < threadCount; ++t) workers.emplace_back(work, t, true);
            } catch (...) {
                launchFailed = true;
                for (auto t = workers.size(); t < threadCount; ++t) start.arrive_and_drop();
            }
            start.arrive_and_wait();
            for (auto& worker : workers) worker.join();
            if (launchFailed) {
                gateOn.store(false, std::memory_order_relaxed); gatedCollector = nullptr;
                return {};
            }
        }
        gateOn.store(false, std::memory_order_relaxed);
        gatedCollector = nullptr;
        Snapshot snapshot{};
        Run result{};
        result.valid = collector.TrySnapshot(SampleBenchQpc, snapshot) && snapshot.faults == 0 && !qpcFailed.load();
        auto earliest = samples.front().begin, latest = samples.front().end;
        for (const auto& sample : samples) {
            result.valid = result.valid && sample.clean && sample.begin > 0 && sample.end >= sample.begin;
            result.threadTicks += sample.end - sample.begin;
            earliest = std::min(earliest, sample.begin);
            latest = std::max(latest, sample.end);
        }
        result.wallTicks = latest - earliest;  // makespan/throughput, distinct from summed per-thread elapsed
        const auto& root = snapshot.categories[static_cast<unsigned>(Category::P5)];
        const auto& child = snapshot.categories[static_cast<unsigned>(Category::Native)];
        result.includedTicks = root.ticks;
        result.calls = root.calls; result.fragments = root.fragments; result.nested = child.nested;
        result.high = snapshot.highWater;
        const std::uint64_t expectedCalls = mode == Mode::BareRoot ? 0 :
            mode == Mode::Root ? static_cast<std::uint64_t>(iterations) * threadCount : threadCount;
        const std::uint64_t expectedNested = mode == Mode::Nested ? static_cast<std::uint64_t>(iterations) * threadCount : 0;
        result.valid = result.valid && result.calls == expectedCalls && result.fragments == expectedCalls &&
                       result.nested == expectedNested && child.calls == 0 && child.ticks == 0;
        return result;
    }
    double Ns(std::uint64_t ticks, std::uint64_t frequency, std::uint64_t divisor)
    {
        return static_cast<double>(ticks) * 1e9 / static_cast<double>(frequency) / static_cast<double>(divisor);
    }
    double DeltaNs(std::uint64_t a, std::uint64_t b, std::uint64_t frequency, std::uint64_t divisor)
    {
        // Preserve negative paired differences from noise; unsigned subtraction
        // or clamping would silently bias the measured distribution upward.
        return (static_cast<double>(a) - static_cast<double>(b)) * 1e9 / static_cast<double>(frequency) / static_cast<double>(divisor);
    }
    void Distribution(std::string_view metric, unsigned threads, const std::vector<double>& input)
    {
        auto values = input;
        std::sort(values.begin(), values.end());
        const auto rank = [&](double q) { return values[static_cast<std::size_t>(std::ceil(q * values.size())) - 1]; };
        double sum = 0;
        for (const auto x : values) sum += x;
        std::cout << "BENCH|summary v=1 metric=" << metric << " threads=" << threads << " chunks=" << values.size()
                  << " distribution=chunk_means unit=ns_per_operation mean=" << sum / values.size()
                  << " min=" << values.front() << " p50=" << rank(.50) << " p95=" << rank(.95)
                  << " p99=" << rank(.99) << " max=" << values.back() << " universal_bound=0\n";
    }
    bool Stage(bool nested, unsigned threads, unsigned chunks, unsigned iterations,
               std::uint64_t frequency, std::uint64_t overallStart)
    {
        const Mode bareMode = nested ? Mode::BareNested : Mode::BareRoot;
        const Mode timedMode = nested ? Mode::Nested : Mode::Root;
        const char* kind = nested ? "nested" : "root";
        // One unreported warm-up per arm at this exact thread count/workload.
        if (!Measure(bareMode, iterations, threads).valid || !Measure(timedMode, iterations, threads).valid) return false;
        std::vector<double> included, includedDelta, externalRaw, externalDelta, throughputDelta;
        for (unsigned chunk = 0; chunk < chunks; ++chunk) {
            if (static_cast<double>(ReadBenchQpc() - overallStart) / frequency > kDeadlineSeconds) return false;
            Run bare{}, timed{};
            if ((chunk & 1u) == 0) { bare = Measure(bareMode, iterations, threads); timed = Measure(timedMode, iterations, threads); }
            else { timed = Measure(timedMode, iterations, threads); bare = Measure(bareMode, iterations, threads); }
            if (!bare.valid || !timed.valid) return false;
            const std::uint64_t operations = static_cast<std::uint64_t>(threads) * iterations;
            included.push_back(Ns(timed.includedTicks, frequency, operations));
            includedDelta.push_back(DeltaNs(timed.includedTicks, bare.includedTicks, frequency, operations));
            externalRaw.push_back(Ns(timed.threadTicks, frequency, operations));
            externalDelta.push_back(DeltaNs(timed.threadTicks, bare.threadTicks, frequency, operations));
            throughputDelta.push_back(DeltaNs(timed.wallTicks, bare.wallTicks, frequency, operations));
            std::cout << "BENCH|chunk v=1 kind=" << kind << " threads=" << threads << " index=" << chunk
                      << " order=" << ((chunk & 1u) ? "timed-bare" : "bare-timed")
                      << " iterations_per_thread=" << iterations << " operations=" << operations
                      << " root_calls=" << timed.calls << " root_fragments=" << timed.fragments << " nested_entries=" << timed.nested
                      << " included_ticks=" << timed.includedTicks << " bare_included_ticks=" << bare.includedTicks
                      << " external_thread_ticks=" << timed.threadTicks << " bare_external_thread_ticks=" << bare.threadTicks
                      << " makespan_ticks=" << timed.wallTicks << " bare_makespan_ticks=" << bare.wallTicks
                      << " high_water=" << timed.high << '\n';
        }
        if (nested) {
            Distribution("nested_included_increment", threads, includedDelta);
            Distribution("nested_external_increment", threads, externalDelta);
            Distribution("nested_makespan_increment", threads, throughputDelta);
        } else {
            Distribution("root_included_empty_span", threads, included);
            Distribution("root_full_external_raw", threads, externalRaw);
            Distribution("root_full_external_minus_bare", threads, externalDelta);
            Distribution("root_makespan_minus_bare", threads, throughputDelta);
        }
        return true;
    }
    int RunBenchmark()
    {
        LARGE_INTEGER qpf{};
        if (!::QueryPerformanceFrequency(&qpf) || qpf.QuadPart <= 0) {
            std::cout << "BENCH|done v=1 status=NO_DATA reason=qpc_frequency\n"; return 2;
        }
        const auto frequency = static_cast<std::uint64_t>(qpf.QuadPart);
        const auto begin = ReadBenchQpc();
        if (!begin) { std::cout << "BENCH|done v=1 status=NO_DATA reason=qpc_read\n"; return 2; }
#if defined(_MSC_FULL_VER)
        constexpr unsigned compiler = _MSC_FULL_VER;
#else
        constexpr unsigned compiler = 0;
#endif
#if defined(NDEBUG)
        constexpr unsigned ndebug = 1;
#else
        constexpr unsigned ndebug = 0;
#endif
        std::cout << std::setprecision(12)
                  << "BENCH|meta v=1 clock=QueryPerformanceCounter frequency=" << frequency
                  << " compiler_msc_full_ver=" << compiler << " ndebug=" << ndebug
                  << " hardware_threads=" << std::thread::hardware_concurrency()
                  << " serial_chunks=" << kSerialChunks << " serial_iterations=" << kSerialIterations
                  << " contended_chunks=" << kContendedChunks << " contended_iterations=" << kContendedIterations
                  << " thread_counts=1,2,4,6 warmup_chunks_per_arm=1 deadline_seconds=" << kDeadlineSeconds
                  << " operation=root_call_or_nested_entry active_gate=emulated_noinline_bool"
                  << " creation_join_output_snapshot=outside_timing universal_bound=0\n";
        for (const unsigned threads : { 1u, 2u, 4u, 6u }) {
            const auto chunks = threads == 1 ? kSerialChunks : kContendedChunks;
            const auto iterations = threads == 1 ? kSerialIterations : kContendedIterations;
            for (const bool nested : { false, true }) {
                if (!Stage(nested, threads, chunks, iterations, frequency, begin)) {
                    std::cout << "BENCH|done v=1 status=NO_DATA reason=deadline_clock_or_accounting incomplete=1\n"; return 2;
                }
            }
        }
        const auto end = ReadBenchQpc();
        if (qpcFailed.load() || end < begin) { std::cout << "BENCH|done v=1 status=NO_DATA reason=qpc_read\n"; return 2; }
        if (static_cast<double>(end - begin) / frequency > kDeadlineSeconds) {
            std::cout << "BENCH|done v=1 status=NO_DATA reason=deadline incomplete=1\n"; return 2;
        }
        std::cout << "BENCH|done v=1 status=COMPLETE elapsed_ms=" << Ns(end-begin, frequency, 1) / 1e6
                  << " empirical_envelope_only=1 chunk_mean_quantiles=1 runtime_controls_required=1\n";
        return 0;
    }
#else
    int RunBenchmark()
    {
        std::cout << "BENCH|done v=1 status=NO_DATA reason=Windows_QPC_required\n";
        return 2;
    }
#endif
}

int main(int argc, char** argv)
{
    if (argc == 2 && std::string_view(argv[1]) == "--bench") {
        try { return cost_bench::RunBenchmark(); }
        catch (...) { std::cout << "BENCH|done v=1 status=NO_DATA reason=benchmark_exception incomplete=1\n"; return 2; }
    }
    if (argc != 1) { std::cerr << "usage: cost_collector_tests [--bench]\n"; return 2; }
    VerifyNativeWrappers();
    Check(CounterSectionValid(kRuntime, kFrameRva + 4, kFrameRva, 4, 0xC0000000), "complete counter inside readable writable data section");
    Check(!CounterSectionValid(kRuntime + 1, kFrameRva + 4, kFrameRva, 4, 0xC0000000), "unknown runtime cannot use known RVA");
    Check(!CounterSectionValid(kRuntime, kFrameRva + 3, kFrameRva, 4, 0xC0000000), "counter cannot straddle image boundary");
    Check(!CounterSectionValid(kRuntime, kFrameRva + 4, kFrameRva, 3, 0xC0000000), "counter cannot straddle section boundary");
    Check(!CounterSectionValid(kRuntime, kFrameRva + 4, kFrameRva, 4, 0x40000000), "read-only section is not writable renderer data");
    Check(!CounterSectionValid(kRuntime, kFrameRva + 4, kFrameRva, 4, 0xE0000000), "executable section cannot be counter data");
    {
        Collector c{ ReadFakeCostClock }; now = 100;
        OwnedJob(c, std::make_unique<OwnedCleanup>());
        auto s = Take(c);
        Check(Count(s, Category::Background).ticks == 150, "owned parameter cleanup precedes timer end and quiescence");
    }
    {
        Collector c{ ReadFakeCostClock };
        now = 10;
        { Scope parent{ &c, Category::P5 }; now = 20; { Scope child{ &c, Category::Native }; now = 30; } now = 100; }
        auto s = Take(c);
        Check(Count(s, Category::P5).ticks == 90 && Count(s, Category::P5).calls == 1, "nested native time included once in parent");
        Check(Count(s, Category::Native).ticks == 0 && Count(s, Category::Native).nested == 1, "nested category records exposure without extra elapsed sum");
    }
    {
        Collector c{ ReadFakeCostClock };
        now = 100;
        { Scope hook{ &c, Category::Look }; now = 130; hook.PauseOriginal();
          auto first = Take(c); Check(Count(first, Category::Look).ticks == 30, "paused original allows a closed pre-fragment boundary");
          now = 140; { Scope reentry{ &c, Category::Native }; now = 160; }
          now = 170; hook.ResumeAfterOriginal(); now = 190; }
        auto s = Take(c);
        Check(Count(s, Category::Look).ticks == 50 && Count(s, Category::Look).fragments == 2 && Count(s, Category::Look).calls == 1, "original engine interval excluded with two complete fragments");
        Check(Count(s, Category::Native).ticks == 20 && Count(s, Category::Native).calls == 1, "plugin reentry during forwarded original remains measured");
    }
    {
        Collector c{ ReadFakeCostClock }; now = 200;
        { Scope p{ &c, Category::P5 }; now = 210;
          { Scope child{ &c, Category::Look }; child.PauseOriginal(); now = 300; child.ResumeAfterOriginal(); }
          now = 400; }
        auto s = Take(c);
        Check(Count(s, Category::P5).ticks == 200 && Count(s, Category::Look).ticks == 0, "nested forwarded original stays inside initiating plugin cost");
    }
    {
        Collector c{ ReadFakeCostClock }; now = 1000;
        std::barrier started(2), finish(2);
        std::thread worker([&] { Scope s{ &c, Category::Background }; started.arrive_and_wait(); finish.arrive_and_wait(); });
        started.arrive_and_wait(); Snapshot s{};
        Check(!c.TrySnapshot(SampleNow, s), "busy background completion prevents partial window snapshot");
        now = 1100; finish.arrive_and_wait(); worker.join();
        s = Take(c);
        Check(Count(s, Category::Background).ticks == 100 && s.busy >= 1, "delayed background duration is retained without waiting for quiescence");
    }
    {
        Collector c{ ReadFakeCostClock }; perThreadClock = true;
        constexpr int threads = 4, iterations = 2000;
        std::barrier start(threads + 1);
        std::vector<std::thread> workers;
        for (int t = 0; t < threads; ++t) workers.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < iterations; ++i) { Scope s{ &c, Category::Event }; Scope nested{ &c, Category::Native }; }
        });
        start.arrive_and_wait();
        for (int i = 0; i < 1000; ++i) {
            Snapshot s{};
            if (c.TrySnapshot(SampleNow, s)) Check(Count(s, Category::Event).calls == Count(s, Category::Event).ticks, "concurrent count/sum snapshots are transactional");
        }
        for (auto& thread : workers) thread.join();
        perThreadClock = false;
        auto s = Take(c);
        Check(Count(s, Category::Event).calls == threads * iterations && Count(s, Category::Event).ticks == threads * iterations, "all concurrent scopes retained exactly once");
        Check(Count(s, Category::Native).nested == threads * iterations, "all concurrent nested exposure retained");
    }
    {
        Windows windows; Window out{}; Snapshot a{}, b{};
        a.stamp = { 1000, 100 }; b.stamp = { 2000, 700 };
        b.categories[0].ticks = 80; b.categories[1].ticks = 20;
        Check(windows.Push(a, 1000, out) == Result::Start, "first snapshot only establishes denominator");
        Check(windows.Push(b, 1000, out) == Result::Ready && out.frames == 600 && out.total == 100, "exact frame delta and total category sum");
        Snapshot empty = b; empty.stamp = { 3000, 1300 };
        Check(windows.Push(empty, 1000, out) == Result::Ready && out.total == 0, "zero-work frames retained in denominator");
        Snapshot reset = empty; reset.stamp = { 4000, 1 };
        Check(windows.Push(reset, 1000, out) == Result::Invalid, "renderer reset invalidates measurement only");
        windows.Reset(); a.stamp = { 1000, 0xfffffff0u }; b = a; b.stamp = { 2000, 20 };
        windows.Push(a, 1000, out);
        Check(windows.Push(b, 1000, out, 16) == Result::Ready && out.frames == 36, "normal u32 renderer wrap preserves exact advance");
        b.generation = 1; b.stamp = { 3000, 700 };
        Check(windows.Push(b, 1000, out) == Result::Gap, "phase/load crossing discards only the incomplete window");
        b.stamp = { 4000, 800 };
        Check(windows.Push(b, 1000, out) == Result::Start, "collection restarts after a short menu or teleport");
        b.stamp = { 5000, 1400 };
        Check(windows.Push(b, 1000, out) == Result::Ready, "later complete phase stays measurable after lifecycle gap");
        windows.Reset(); a = {}; a.stamp = { 1000, 1 }; b = a; b.stamp.qpc = 62000;
        windows.Push(a, 1000, out);
        Check(windows.Push(b, 1000, out) == Result::Invalid, "stale frame counter cannot produce green empty windows");
    }
    {
        Collector c{ ReadFakeCostClock }; Snapshot s{};
        Check(!c.TrySnapshot(BadSample, s), "counter read failure rejects snapshot");
        Check(c.TrySnapshot(SampleNow, s) && s.faults == 1, "read failure remains a recorded measurement fault");
        Windows windows; Window out{};
        Check(windows.Push(s, 1000, out) == Result::Invalid, "known measurement fault cannot reset to a pass");
    }
    std::cout << "cost_collector_tests checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
