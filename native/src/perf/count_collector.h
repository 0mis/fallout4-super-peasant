#pragma once
// Test-only exact counted windows. No game headers, allocations or I/O.
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <type_traits>
#include <utility>

namespace sp::countcost
{
    enum class Category : unsigned { P5, P6, P1, P2, P3, P4, P7, P8, P9, Look, Native, Event, Input, Lifecycle, Background, Count };
    inline constexpr unsigned kCategories = static_cast<unsigned>(Category::Count);
    inline constexpr const char* kNames[kCategories] = { "P5", "P6", "P1", "P2", "P3", "P4", "P7", "P8", "P9", "Look", "Native", "Event", "Input", "Lifecycle", "Background" };
    inline constexpr const char* kPolicy = "native-all-entries-quiescent-v1";
    inline constexpr std::uint32_t kRuntime = 0x010B0F00, kFrameRva = 0x3D87238;
    constexpr bool CounterSectionValid(std::uint32_t runtime, std::uint32_t imageSize, std::uint32_t sectionStart,
                                       std::uint32_t sectionSize, std::uint32_t flags) noexcept {
        const std::uint64_t end = static_cast<std::uint64_t>(kFrameRva) + sizeof(std::uint32_t);
        return runtime == kRuntime && end <= imageSize && sectionStart <= kFrameRva &&
               end <= static_cast<std::uint64_t>(sectionStart) + sectionSize &&
               (flags & 0xC0000000u) == 0xC0000000u && (flags & 0x20000000u) == 0;
    }
    struct Stamp { std::uint64_t qpc{}; std::uint32_t frame{}; };
    struct Counts { std::uint64_t calls{}, fragments{}, ticks{}, nested{}; };
    struct Snapshot {
        Stamp stamp{};
        std::array<Counts, kCategories> categories{};
        std::uint64_t generation{}, faults{}, busy{}, highWater{}, liveP5{}, acceptedP6{}, playerP7{};
    };
    using Clock = std::uint64_t (*)() noexcept;
    using Sample = bool (*)(Stamp&) noexcept;

    class Collector
    {
    public:
        explicit Collector(Clock clock) noexcept : _clock(clock) {}
        std::uint64_t Begin(Category category, bool first) noexcept
        {
            try {
                std::scoped_lock lock{ _lock };
                auto& c = _counts[static_cast<unsigned>(category)];
                ++_active;
                if (_active > _highWater) _highWater = _active;
                if (first) ++c.calls;
                return _clock();
            } catch (...) { Fault(); return 0; }
        }
        void End(Category category, std::uint64_t start) noexcept
        {
            const auto end = _clock();
            try {
                std::scoped_lock lock{ _lock };
                auto& c = _counts[static_cast<unsigned>(category)];
                if (_active == 0 || !start || end < start || c.ticks > std::numeric_limits<std::uint64_t>::max() - (end - start)) {
                    Fault();
                } else {
                    ++c.fragments;
                    c.ticks += end - start;
                }
                if (_active) --_active;
            } catch (...) { Fault(); }
        }
        void Nested(Category category) noexcept { _nested[static_cast<unsigned>(category)].fetch_add(1, std::memory_order_relaxed); }
        void Fault() noexcept { _faults.fetch_add(1, std::memory_order_relaxed); }
        std::uint64_t Faults() const noexcept { return _faults.load(std::memory_order_relaxed); }
        void Discontinuity() noexcept { _generation.fetch_add(1, std::memory_order_relaxed); }
        void Note(unsigned which) noexcept { if (which < 3) _notes[which].fetch_add(1, std::memory_order_relaxed); }
        bool TrySnapshot(Sample sample, Snapshot& out) noexcept
        {
            try {
                std::unique_lock lock{ _lock, std::try_to_lock };
                if (!lock.owns_lock() || _active != 0) { _busy.fetch_add(1, std::memory_order_relaxed); return false; }
                Stamp stamp{};
                if (!sample(stamp) || !stamp.qpc) { Fault(); return false; }
                out = {};
                out.stamp = stamp;
                out.categories = _counts;
                for (unsigned i = 0; i < kCategories; ++i) out.categories[i].nested = _nested[i].load(std::memory_order_relaxed);
                out.generation = _generation.load(std::memory_order_relaxed);
                out.faults = _faults.load(std::memory_order_relaxed);
                out.busy = _busy.load(std::memory_order_relaxed);
                out.highWater = _highWater;
                out.liveP5 = _notes[0].load(std::memory_order_relaxed);
                out.acceptedP6 = _notes[1].load(std::memory_order_relaxed);
                out.playerP7 = _notes[2].load(std::memory_order_relaxed);
                return true;
            } catch (...) { Fault(); return false; }
        }
    private:
        Clock _clock;
        std::mutex _lock;
        std::uint64_t _active{}, _highWater{};
        std::array<Counts, kCategories> _counts{};
        std::array<std::atomic<std::uint64_t>, kCategories> _nested{};
        std::atomic<std::uint64_t> _generation{}, _faults{}, _busy{};
        std::array<std::atomic<std::uint64_t>, 3> _notes{};
    };

    // Each thread has one current root collector. Nested plugin reentry is charged
    // to its ancestor. A forwarded original pauses only the outermost hook.
    struct ThreadState { Collector* collector{}; unsigned depth{}; };
    inline thread_local ThreadState tl{};
    // Function parameters outlive local timing scopes. Release their owned data
    // before the scope exits without changing ownership while the job executes.
    template <class T> struct ParameterTail {
        static_assert(std::is_nothrow_default_constructible_v<T> && std::is_nothrow_swappable_v<T>);
        T& value;
        ~ParameterTail() noexcept { T retired{}; using std::swap; swap(value, retired); }
    };
    class Scope
    {
    public:
        explicit Scope(Category category) noexcept : _category(category) {}
        Scope(Collector* collector, Category category) noexcept : _category(category) { Start(collector); }
        void Start(Collector* collector) noexcept
        {
            if (_collector) { _collector->Fault(); return; }
            _collector = collector;
            if (!_collector) return;
            if (tl.depth && tl.collector != _collector) { _collector->Fault(); _collector = nullptr; return; }
            _root = tl.depth++ == 0;
            if (_root) { tl.collector = collector; _start = collector->Begin(_category, true); }
            else collector->Nested(_category);
        }
        ~Scope() noexcept
        {
            if (!_collector || _paused) return;
            if (_root) _collector->End(_category, _start);
            if (tl.depth) --tl.depth; else _collector->Fault();
            if (!tl.depth) tl.collector = nullptr;
        }
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
        void PauseOriginal() noexcept
        {
            if (!_collector || !_root || _paused) return;
            _collector->End(_category, _start);
            if (tl.depth != 1) _collector->Fault();
            tl = {};
            _paused = true;
        }
        void ResumeAfterOriginal() noexcept
        {
            if (!_collector || !_root || !_paused) return;
            if (tl.depth != 0) { _collector->Fault(); return; }
            tl = { _collector, 1 };
            _start = _collector->Begin(_category, false);
            _paused = false;
        }
    private:
        Collector* _collector{};
        Category _category{};
        std::uint64_t _start{};
        bool _root{}, _paused{};
    };

    struct Window { Snapshot begin{}, end{}; std::uint32_t frames{}; std::uint64_t total{}; };
    enum class Result { Start, Pending, Ready, Gap, Invalid };
    class Windows
    {
    public:
        Result Push(const Snapshot& now, std::uint64_t frequency, Window& out, std::uint32_t minimumFrames = 600) noexcept
        {
            if (!frequency || now.faults) { _have = false; return Result::Invalid; }
            if (!_have) { _begin = now; _last = now; _have = true; return Result::Start; }
            if (now.generation != _begin.generation) {
                out = { _begin, now, 0, 0 };  // deliberately discard this incomplete lifecycle-crossing interval
                _have = false;
                return Result::Gap;
            }
            const auto d = static_cast<std::uint32_t>(now.stamp.frame - _last.stamp.frame);
            if (now.stamp.qpc <= _last.stamp.qpc || d > 10000 ||
                now.stamp.qpc - _begin.stamp.qpc > frequency * 60) {
                _have = false; return Result::Invalid;
            }
            _last = now;
            const auto frames = static_cast<std::uint32_t>(now.stamp.frame - _begin.stamp.frame);
            if (frames < minimumFrames) return Result::Pending;
            const double seconds = static_cast<double>(now.stamp.qpc - _begin.stamp.qpc) / static_cast<double>(frequency);
            if (static_cast<double>(frames) / seconds > 10000.0) { _have = false; return Result::Invalid; }
            std::uint64_t total = 0;
            for (unsigned i = 0; i < kCategories; ++i) {
                const auto& a = _begin.categories[i]; const auto& b = now.categories[i];
                if (b.calls < a.calls || b.fragments < a.fragments || b.ticks < a.ticks || b.nested < a.nested ||
                    total > std::numeric_limits<std::uint64_t>::max() - (b.ticks - a.ticks)) {
                    _have = false; return Result::Invalid;
                }
                total += b.ticks - a.ticks;
            }
            out = { _begin, now, frames, total };
            _begin = now;
            return Result::Ready;
        }
        void Reset() noexcept { _have = false; }
    private:
        Snapshot _begin{}, _last{};
        bool _have{};
    };
}
