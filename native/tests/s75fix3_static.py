"""R6 STEP 7.5 FIX LANE 3: static checks of the game-only trail glue fixes (src/fo4 cannot run on the desktop).

  FX-01 REVIEW 1 (crash on the fallback): the trail root is made with NO child slots of ours (new RE::NiNode(0): the
        engine's own AttachChild allocates / regrows the array on its heap) and is never DecRefCount-ed (refusal: hidden
        and leaked, HOOKS G1).
  FX-02 REVIEW 4 (drift): the root cancels the render camera (Main::WorldRootCamera, its own byte check with a logged
        fall-back to the old cancel), only inside the SEH-shielded root update; every drawn frame shifted more than
        trails.driftLog u is logged with its frame and thread; the stat line keeps its old fields and adds the new ones.
  FX-03 core/trails_cancel.h is pure (no game headers).
  FX-04 the shipped ini: trails.cancel 1, brain.defendReReach 1; LF, no BOM; the always-rules keys; no TEST keys.
Run from anywhere: uv run --no-project python -I tests/s75fix3_static.py [tree]  (exit 0 = all PASS; the step-7.5 tree fails FX-01 / 02 / 04)
"""
import os
import re
import sys

ROOT = os.path.normpath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))  # optional: another tree
fails = 0


def check(cond, what):
    global fails
    print(("PASS " if cond else "FAIL ") + what)
    if not cond:
        fails += 1


def read(rel):
    with open(os.path.join(ROOT, rel), "rb") as f:
        return f.read()


glue = read("src/fo4/trails_glue.cpp").decode("utf-8")
code = re.sub(r"//[^\n]*", "", glue)  # comments out

# FX-01
check("new RE::NiNode(0)" in code, "FX-01 the trail root is made with 0 child slots (the engine allocates the array)")
check(re.search(r"new\s+RE::NiNode\((?!0\))", code) is None, "FX-01 no NiNode with a plugin-allocated child array")
check(re.search(r"\broot\s*->\s*DecRefCount\s*\(", code) is None, "FX-01 the root is never DecRefCount-ed (refusal: hidden + leaked)")
make = code[code.find("bool MakeImpl"):code.find("bool MakeSEH")]
check(make.count("return false;") >= 4 and "SetAppCulled(true)" in make, "FX-01 every refusal returns with the root app-culled")

# FX-02
check(re.search(r'kCamFn\{\s*"Main::WorldRootCamera",\s*2228956,\s*0xC37DD0,\s*"48 8B 05 xx xx xx xx 48 8B 80 40 01 00 00 C3"\s*\}', code) is not None,
      "FX-02 the camera function has its own id / rva / byte check")
check("falls back to the parent's world" in glue and "g_e.camOk = true" in code, "FX-02 a camera check mismatch logs and falls back (camOk false)")
upd_seh = code[code.find("bool UpdateRootSEH"):]
upd_seh = upd_seh[:upd_seh.find("}\n\t\t}") + 5]
check("__try" in upd_seh and "UpdateRootImpl(" in upd_seh and "__except" in upd_seh, "FX-02 the root update (camera read included) runs inside its SEH shield")
calls = [m.start() for m in re.finditer(r"RE::Main::WorldRootCamera\(\)", code)]
impl_a, impl_b = code.find("void UpdateRootImpl"), code.find("bool UpdateRootSEH")
check(len(calls) == 1 and impl_a < calls[0] < impl_b, "FX-02 WorldRootCamera is called only from UpdateRootImpl")
check("UpdateRootImpl(" in code and code.count("UpdateRootImpl(") == 2, "FX-02 UpdateRootImpl has exactly one caller (the shield)")
check("g_e.camOk && g_parentMode == 1" in code, "FX-02 the camera cancel only under the Weather node (a static parent keeps the old cancel)")
check(re.search(r'SPN\|TRAIL\|drift \{:\.1f\} u .*frame=\{\} thread=\{\}', glue) is not None, "FX-02 a drift line per shifted drawn frame with frame and thread")
check("g_driftLogs <= 20 || (g_driftLogs % 100) == 0" in code, "FX-02 the drift line is rate-limited (20, then every 100th)")
stat = re.search(r'return std::format\("on=\{\}.*?"', glue, re.S)
check(stat is not None and "drift={:.1f} frameCost={:.1f} cost avg {:.1f} us max {:.0f} us cancel={} driftAlt=" in stat.group(0),
      "FX-02 the stat line keeps every old field in place and appends cancel / driftAlt / driftFrames / driftOver")
check('Tunable("trails.cancel", 0.0)' in code, "FX-02 code default trails.cancel 0 (= step 7.5)")

# FX-03
core = read("src/core/trails_cancel.h").decode("utf-8") if os.path.exists(os.path.join(ROOT, "src/core/trails_cancel.h")) else "#include <game>"
incs = re.findall(r'#include\s+[<"]([^>"]+)[>"]', core)
check(all(i in ("core/math.h", "algorithm", "cstdint") for i in incs), "FX-03 core/trails_cancel.h is pure: %s" % incs)

# FX-04
ini = read("SuperPeasantNative.ini")
check(not ini.startswith(b"\xef\xbb\xbf"), "FX-04 ini: no BOM")
check(b"\r" not in ini, "FX-04 ini: LF only")
txt = ini.decode("utf-8")
live = [l.split(";")[0].strip() for l in txt.splitlines() if l.strip() and not l.strip().startswith(";")]


def key(k):
    for l in live:
        if "=" in l and l.split("=")[0].strip().lower() == k.lower():
            return l.split("=", 1)[1].strip()
    return None


check(key("trails.cancel") == "1", "FX-04 ini: trails.cancel = 1")
check(key("brain.defendReReach") == "1", "FX-04 ini: brain.defendReReach = 1")
check(key("fo4.forceConvention") == "1" and key("brain.range") == "1" and key("brain.requireCombat") == "0" and key("Native") == "1",
      "FX-04 ini: fo4.forceConvention 1, brain.range 1, brain.requireCombat 0, [Brain] Native 1")
test_keys = [l for l in live if re.match(r"(trails\.(probe|diag|keyColor)|debug\.)", l, re.I)]
check(not test_keys and not re.search(r"^\s*\[Debug\]", txt, re.M | re.I), "FX-04 ini: no TEST keys / [Debug] section %s" % test_keys)
check(b"\x08" not in ini, "FX-04 ini: no raw control bytes")

print("s75fix3_static: %s (%d failed)" % ("ALL PASS" if not fails else "FAIL", fails))
sys.exit(1 if fails else 0)
