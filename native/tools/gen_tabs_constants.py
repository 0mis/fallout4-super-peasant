"""Generate src/config_tabs.gen.h from FO4W/v2/spec/tabs_constants.json.

Every numeric / bool / string leaf of the JSON becomes an `inline constexpr` under
`sp::tabs::<section>::<key>...` (nested namespaces follow the JSON nesting).
  numbers  -> double (ints that fit stay std::int64_t), bools -> bool, strings -> std::string_view
  numeric lists -> std::array<double, N>; rectangular numeric 2D lists -> std::array<std::array<double, M>, N>
  string lists -> std::array<std::string_view, N>; lists of objects -> namespace per element
  (named after the element's "name" field when present, else e<i>).
Identifiers are sanitised (non-alnum -> '_', leading digit/underscore -> 'k', C++ keywords -> trailing '_').
Usage: uv run --no-project python -I tools/gen_tabs_constants.py <tabs_constants.json> <out.h>
Do not edit the output by hand; rerun this script.
"""
import json
import re
import sys

KEYWORDS = set("""alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
char32_t class compl concept const consteval constexpr constinit const_cast continue co_await co_return co_yield
decltype default delete do double dynamic_cast else enum explicit export extern false float for friend goto if
inline int long mutable namespace new noexcept not not_eq nullptr operator or or_eq private protected public
register reinterpret_cast requires return short signed sizeof static static_assert static_cast struct switch
template this thread_local throw true try typedef typeid typename union unsigned using virtual void volatile
wchar_t while xor xor_eq min max near far small interface""".split())


def ident(name, used):
    s = re.sub(r"[^0-9A-Za-z_]", "_", str(name))
    if not s or s[0].isdigit() or s[0] == "_":
        s = "k" + s
    if s in KEYWORDS:
        s += "_"
    base, i = s, 2
    while s in used:
        s = f"{base}_{i}"
        i += 1
    used.add(s)
    return s


def num(v):
    if isinstance(v, bool):
        return "1.0" if v else "0.0"
    if isinstance(v, int):
        return f"{float(v)!r}"
    r = repr(float(v))
    if r in ("nan", "inf", "-inf"):
        return {"nan": "std::numeric_limits<double>::quiet_NaN()", "inf": "std::numeric_limits<double>::infinity()",
                "-inf": "-std::numeric_limits<double>::infinity()"}[r]
    return r


def is_num(v):
    return isinstance(v, (int, float)) and not isinstance(v, bool) or isinstance(v, bool)


def strlit(s):
    # split into <=1500-char raw pieces (MSVC literal piece limit 16 KB)
    s = str(s)
    pieces = [s[i:i + 1500] for i in range(0, len(s), 1500)] or [""]
    out = []
    for p in pieces:
        d = "~"
        while f"){d}\"" in p:
            d += "~"
        out.append(f'R"{d}({p}){d}"')
    return " ".join(out)


def emit(o, ind, out):
    used = set()
    pad = "    " * ind
    for k, v in o.items():
        name = ident(k, used)
        emit_value(name, v, ind, out, used)


def emit_value(name, v, ind, out, used):
    pad = "    " * ind
    if isinstance(v, dict):
        out.append(f"{pad}namespace {name} {{")
        emit(v, ind + 1, out)
        out.append(f"{pad}}}")
    elif isinstance(v, list):
        if not v:
            out.append(f"{pad}inline constexpr std::size_t {name}_count = 0;")
        elif all(is_num(x) for x in v):
            vals = ", ".join(num(x) for x in v)
            out.append(f"{pad}inline constexpr std::array<double, {len(v)}> {name}{{ {vals} }};")
        elif all(isinstance(x, list) and x and all(is_num(y) for y in x) for x in v) and len({len(x) for x in v}) == 1:
            m = len(v[0])
            rows = ", ".join("std::array<double, %d>{ %s }" % (m, ", ".join(num(y) for y in x)) for x in v)
            out.append(f"{pad}inline constexpr std::array<std::array<double, {m}>, {len(v)}> {name}{{ {rows} }};")
        elif all(isinstance(x, str) for x in v):
            vals = ", ".join(f"std::string_view{{ {strlit(x)} }}" for x in v)
            out.append(f"{pad}inline constexpr std::array<std::string_view, {len(v)}> {name}{{ {vals} }};")
        else:
            out.append(f"{pad}namespace {name} {{")
            out.append(f"{pad}    inline constexpr std::size_t count = {len(v)};")
            inner = {"count"}
            for i, x in enumerate(v):
                ename = f"e{i}"
                if isinstance(x, dict) and isinstance(x.get("name"), str):
                    ename = x["name"]
                emit_value(ident(ename, inner), x, ind + 1, out, inner)
            out.append(f"{pad}}}")
    elif isinstance(v, bool):
        out.append(f"{pad}inline constexpr bool {name} = {'true' if v else 'false'};")
    elif isinstance(v, int):
        out.append(f"{pad}inline constexpr std::int64_t {name} = {v}LL;" if abs(v) < 2**62 else
                   f"{pad}inline constexpr double {name} = {num(v)};")
    elif isinstance(v, float):
        out.append(f"{pad}inline constexpr double {name} = {num(v)};")
    elif isinstance(v, str):
        out.append(f"{pad}inline constexpr std::string_view {name}{{ {strlit(v)} }};")
    elif v is None:
        out.append(f"{pad}// {name} = null")


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, encoding="utf-8") as f:
        d = json.load(f)
    out = ["// GENERATED by tools/gen_tabs_constants.py from v2/spec/tabs_constants.json. DO NOT EDIT.",
           "// Units: SI (m, kg, s, rad) unless the key says otherwise; Unity frame (y up). See TABS_MOTION_SPEC.md.",
           "#pragma once", "#include <array>", "#include <cstddef>", "#include <cstdint>", "#include <limits>",
           "#include <string_view>", "", "// clang-format off", "namespace sp::tabs {"]
    emit(d, 1, out)
    out.append("}  // namespace sp::tabs")
    out.append("// clang-format on")
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print(f"wrote {dst}: {len(out)} lines")


if __name__ == "__main__":
    main()
