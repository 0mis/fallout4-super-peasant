#define SP_COST_PROBE 1
#include "perf/cost_probe.h"
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace sp::costprobe { countcost::Collector* Active() noexcept { return nullptr; } }
namespace {
    int NativeInt(int& value, std::unique_ptr<int> input) { value += *input; return value; }
    void NativeVoid(std::vector<int> input, int& out) { out = input.at(0); }
    static_assert(std::is_same_v<decltype(&NativeInt), decltype(&SP_COST_NATIVE(NativeInt))>);
    static_assert(std::is_same_v<decltype(&NativeVoid), decltype(&SP_COST_NATIVE(NativeVoid))>);
}
void VerifyNativeWrappers()
{
    int value = 4;
    auto* wrapped = SP_COST_NATIVE(NativeInt);
    if (wrapped(value, std::make_unique<int>(3)) != 7 || value != 7) throw std::runtime_error("native wrapper changed reference/move/return semantics");
    SP_COST_NATIVE(NativeVoid)(std::vector<int>{ 9 }, value);
    if (value != 9) throw std::runtime_error("void native wrapper changed value ownership");
}
