#pragma once
// OFF is a token-level no-op: original native targets, no collector or startup.
#ifndef SP_COST_PROBE
#define SP_COST_PROBE 0
#endif
#if SP_COST_PROBE
#include "perf/count_collector.h"
#include <utility>
namespace sp::costprobe {
    countcost::Collector* Active() noexcept;
    void Initialize(std::uint32_t runtimeVersion) noexcept;
    void Pump() noexcept;
    void Discontinuity() noexcept;
    void Note(unsigned which) noexcept;
    template <class Signature> struct Native;
    template <class R, class... A> struct Native<R (*)(A...)> {
        template <R (*Function)(A...)>
        static R Invoke(A... args) {
            countcost::Scope scope{ Active(), countcost::Category::Native };
            return Function(std::forward<A>(args)...);
        }
    };
}
#define SP_COST_SCOPE(category) ::sp::countcost::Scope _spCostScope{ ::sp::costprobe::Active(), ::sp::countcost::Category::category }
#define SP_COST_DEFERRED_SCOPE(category) ::sp::countcost::Scope _spCostScope{ ::sp::countcost::Category::category }
#define SP_COST_START() _spCostScope.Start(::sp::costprobe::Active())
#define SP_COST_PARAMETER_TAIL(parameter) ::sp::countcost::ParameterTail<decltype(parameter)> _spCostTail_##parameter{ parameter }
#define SP_COST_PAUSE() _spCostScope.PauseOriginal()
#define SP_COST_RESUME() _spCostScope.ResumeAfterOriginal()
#define SP_COST_NATIVE(function) ::sp::costprobe::Native<decltype(&function)>::template Invoke<&function>
#define SP_COST_INIT(runtime) ::sp::costprobe::Initialize(runtime)
#define SP_COST_PUMP() ::sp::costprobe::Pump()
#define SP_COST_GAP() ::sp::costprobe::Discontinuity()
#define SP_COST_NOTE(which) ::sp::costprobe::Note(which)
#else
#define SP_COST_SCOPE(category)
#define SP_COST_DEFERRED_SCOPE(category)
#define SP_COST_START()
#define SP_COST_PARAMETER_TAIL(parameter)
#define SP_COST_PAUSE()
#define SP_COST_RESUME()
#define SP_COST_NATIVE(function) function
#define SP_COST_INIT(runtime)
#define SP_COST_PUMP()
#define SP_COST_GAP()
#define SP_COST_NOTE(which)
#endif
