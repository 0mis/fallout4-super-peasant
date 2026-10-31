"""Offline tests for build_esp.py and validate_esp.py (agent B2). Stdlib only; run with -I -B.

  python -I -B tools/esp/test_build_esp.py [-v]

Writes only under work/plugin/t/ (deleted and recreated each run). Last line: RESULT pass=N fail=M.
  B1  build into work/plugin/t twice: exit 0, byte-identical (deterministic), identical to build/Data if present
  B2  every VMAD property type the builder supports (Object, alias, Int, Float, Bool, String, arrays) encodes and
      decodes back (vmad.py round trip), and an unbuilt P2 target is left out of the VMAD
  B3  --no-p1 --no-p2: still validates (the P0 perk stays listed, SP_PunchFX left out of the VMAD); PRKR rank is 1
  V1  the fresh build validates with 0 FAIL (scripts in --partial mode)
  V2  mutation tests: each plants one defect in a copy of the plugin (or of the .psc tree) and requires the named
      validator check to FAIL, while the unmutated copy passes it
  P1  the .psc parser on tricky source (comments, doc comments, continuation, full properties, structs, groups)
  P2  every compiled .pex under build/Data/Scripts parses and lists exactly the .psc's properties
"""
import contextlib
import io
import json
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import build_esp as B  # noqa: E402
import esm_reader as er  # noqa: E402
import esp_writer as ew  # noqa: E402
import validate_esp as V  # noqa: E402
import vmad as vm  # noqa: E402

FO4W = B.FO4W
T = os.path.join(FO4W, "work", "plugin", "t")
VERBOSE = "-v" in sys.argv


class Tally(object):
    def __init__(self):
        self.p = self.f = 0

    def check(self, ok, name, detail=""):
        if ok:
            self.p += 1
        else:
            self.f += 1
        if VERBOSE or not ok:
            print("%s %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""))


def quiet(fn, *a):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = fn(*a)
    return rc, buf.getvalue()


def build_to(d, extra=()):
    os.makedirs(d, exist_ok=True)
    m, t = os.path.join(d, "SuperPeasant.esp"), os.path.join(d, "SuperPeasant_Test.esp")
    rc, out = quiet(B.main, ["--out-main", m, "--out-test", t, "--report", os.path.join(d, "report.json"), "-q"] +
                    list(extra))
    return rc, out, m, t


def validate(m, t, extra=()):
    rc, out = quiet(V.main, ["--main", m, "--test", t, "--partial", "-q"] + list(extra))
    fails = [l for l in out.splitlines() if l.startswith("FAIL ")]
    return rc, fails, out


# ------------------------------------------------------------------ mutation helpers
def mutate(src, dst, fn):
    p = er.Plugin.load(src)
    for r in p.iter_records():
        r.subrecords  # noqa: B018
    fn(p)
    ew.write_plugin(p, dst)


def rec(p, edid):
    for r in p.iter_records():
        if r.edid == edid:
            return r
    raise KeyError(edid)


def edit_vmad(p, edid, fn):
    r = rec(p, edid)
    s = r.get("VMAD")
    d = vm.decode(s.data, r.sig)
    fn(d)
    s.data = vm.encode(d, r.sig)


def prop(d, script, name):
    for s in d["scripts"]:
        if s["name"] == script:
            for x in s["properties"]:
                if x["name"] == name:
                    return s, x
    raise KeyError(name)


def m_prop_renamed(p):
    edit_vmad(p, "SP_Main", lambda d: prop(d, "SuperPeasant:Controller", "SP_Debug")[1].update(name="SP_Debugx"))


def m_prop_wrong_type(p):
    def f(d):
        prop(d, "SuperPeasant:Controller", "SP_Waiting")[1]["value"]["formid"] = 0x01000807   # an OTFT
    edit_vmad(p, "SP_Main", f)


def m_prop_removed(p):
    def f(d):
        s, x = prop(d, "SuperPeasant:Brain", "SP_ThreatKeywords")
        s["properties"].remove(x)
    edit_vmad(p, "SP_BrainQuest", f)


def m_alias_bad(p):
    def f(d):
        prop(d, "SuperPeasant:Controller", "PeasantAlias")[1]["value"]["alias"] = 5
    edit_vmad(p, "SP_Main", f)


def m_cond_removed(p):
    ew.remove_subs(rec(p, "SP_Follow"), "CTDA")


def m_cond_wrong(p):
    r = rec(p, "SP_Wait")
    s = r.get("CTDA")
    s.data = s.data[:4] + struct.pack("<f", 0.0) + s.data[8:]


def m_lstring(p):
    ew.set_sub(rec(p, "SP_SuperPeasant"), "FULL", struct.pack("<I", 0x1407))


def m_bad_model(p):
    ew.set_sub(rec(p, "SP_BodyAA"), "MOD2", ew.zstr("SuperPeasant\\Nope.nif"))


def m_unresolved(p):
    ew.set_sub(rec(p, "SP_SuperPeasant"), "RNAM", struct.pack("<I", 0x00FFFFF0))


def m_wrong_ref_type(p):
    ew.set_sub(rec(p, "SP_Outfit"), "INAM", struct.pack("<I", 0x01000811))       # a GLOB in an outfit


def m_alias_flags(p):
    r = rec(p, "SP_Main")
    fn = [s for s in r.subrecords if s.sig == "FNAM"][1]
    fn.data = struct.pack("<I", 0x2)


def m_glob_value(p):
    ew.set_sub(rec(p, "SP_DashSpeedMax"), "FLTV", struct.pack("<f", 4000.0))


def m_mgef_flags(p):
    r = rec(p, "SP_PunchEffect")
    d = bytearray(r.get("DATA").data)
    struct.pack_into("<I", d, 0, 0x1C008011)                                      # + Hostile
    r.get("DATA").data = bytes(d)


def m_extra_record(p):
    ew.add_record(p, ew.new_record("GLOB", 0x01000830, [er.Subrecord("EDID", ew.zstr("SP_Extra")),
                                                         er.Subrecord("FLTV", struct.pack("<f", 1.0))]))


def m_button(p):
    r = rec(p, "SP_MenuMsg")
    its = [s for s in r.subrecords if s.sig == "ITXT"]
    its[0].data, its[1].data = its[1].data, its[0].data


def m_dup_group(p):
    p.groups.append(er.Group("GLOB", 0, [ew.new_record("GLOB", 0x01000831, [
        er.Subrecord("EDID", ew.zstr("SP_Dup")), er.Subrecord("FNAM", b"f"), er.Subrecord("FLTV", b"\0" * 4)])]))


def m_hedr(p):
    pass  # handled by writing with update_hedr=False after a manual count change (see below)


def m_sndr_loop_cleared(p):
    ew.set_sub(rec(p, "SP_FistLoopSNDR"), "LNAM", bytes(4))


def m_sndr_file_missing(p):
    r = rec(p, "SP_SwingSNDR")
    r.get("ANAM").data = ew.zstr(r"Data\Sound\FX\SuperPeasant\Swing\Nope.wav")


def m_sndr_category_glob(p):
    ew.set_sub(rec(p, "SP_HitSNDR"), "GNAM", struct.pack("<I", 0x01000811))       # a GLOB, not an SNCT


def m_sndr_pitch_variance(p):
    r = rec(p, "SP_VocalSNDR")
    r.get("BNAM").data = bytes([0, 10]) + r.get("BNAM").data[2:]                    # +10 % frequency variance


# C247 (work/c247/PLAN_247.md S2 / M9): the belts the real-save play-test relies on
def m_voice_default(p):
    ew.set_sub(rec(p, "SP_SuperPeasant"), "VTCK", struct.pack("<I", 0x0000002D))   # MaleRough: generic barks again


def m_stealth_flag_cleared(p):
    r = rec(p, "SP_SuperPeasant")
    d = bytearray(r.get("ACBS").data)
    struct.pack_into("<I", d, 0, struct.unpack_from("<I", d)[0] & ~0x40)            # Doesn't affect stealth meter
    r.get("ACBS").data = bytes(d)


def m_alias_is_companion(p):
    r = rec(p, "SP_Main")
    fn = [s for s in r.subrecords if s.sig == "FNAM"][1]
    fn.data = struct.pack("<I", struct.unpack("<I", fn.data)[0] | (1 << 23))       # Is Companion: never


def m_debug_default_on(p):
    ew.set_sub(rec(p, "SP_Debug"), "FLTV", struct.pack("<f", 1.0))                 # release default is 0


def m_test_master_index(p):
    def f(d):
        prop(d, "SuperPeasant:Test:Runner", "Controller")[1]["value"]["formid"] = 0x02000800   # itself, not SP_Main
    edit_vmad(p, "SPTest_Main", f)


# CARRY (work/carry/CARRY_SPEC.md 9.7): SP_MenuMsg "Carry me" / "Put me down" are shown by GetGlobalValue(SP_Carrying)
def m_mesg_cond_removed(p):
    ew.remove_subs(rec(p, "SP_MenuMsg"), "CTDA")                                      # both buttons always shown


def m_mesg_cond_wrong(p):
    s = rec(p, "SP_MenuMsg").get("CTDA")                                              # "Carry me" shown while carrying
    s.data = s.data[:4] + struct.pack("<f", 1.0) + s.data[8:]


MUTATIONS = [
    # (name, which plugin, mutator, substring of a FAIL line that must appear)
    ("VMAD property renamed", "main", m_prop_renamed, "SuperPeasant:Controller properties = contract"),
    ("VMAD property renamed -> psc", "main", m_prop_renamed, "every Mandatory property is filled"),
    ("VMAD property points at wrong type", "main", m_prop_wrong_type, "every filled value fits the declared type"),
    ("VMAD Mandatory property removed", "main", m_prop_removed, "SuperPeasant:Brain properties = contract"),
    ("VMAD alias property names alias 5", "main", m_alias_bad, "resolves to an allowed type"),
    ("PACK condition removed", "main", m_cond_removed, "PACK SP_Follow fields = contract spec"),
    ("PACK condition value 0 instead of 1", "main", m_cond_wrong, "PACK SP_Wait fields = contract spec"),
    ("localized string left as 4-byte ID", "main", m_lstring, "inline printable strings"),
    ("ARMA model path missing", "main", m_bad_model, "every model path exists"),
    ("ARMA model path != contract", "main", m_bad_model, "ARMA SP_BodyAA fields = contract spec"),
    ("unresolved FormID", "main", m_unresolved, "resolves to an allowed type"),
    ("FormID of the wrong type", "main", m_wrong_ref_type, "resolves to an allowed type"),
    ("alias flags", "main", m_alias_flags, "QUST SP_Main fields = contract spec"),
    ("GLOB default value", "main", m_glob_value, "GLOB SP_DashSpeedMax fields = contract spec"),
    ("MGEF Hostile flag", "main", m_mgef_flags, "MGEF SP_PunchEffect fields = contract spec"),
    ("record not in the contract", "main", m_extra_record, "records = contract"),
    ("menu buttons swapped", "main", m_button, "MESG SP_MenuMsg fields = contract spec"),
    ("duplicated top group", "main", m_dup_group, "no duplicated top group"),
    ("SNDR loop flag cleared", "main", m_sndr_loop_cleared, "SNDR SP_FistLoopSNDR fields = contract spec"),
    ("SNDR file not staged", "main", m_sndr_file_missing, "every SNDR sound file is staged"),
    ("SNDR file != contract ANAM", "main", m_sndr_file_missing, "SNDR SP_SwingSNDR fields = contract spec"),
    ("SNDR category is a GLOB", "main", m_sndr_category_glob, "resolves to an allowed type"),
    ("SNDR pitch randomisation", "main", m_sndr_pitch_variance, "SNDR SP_VocalSNDR fields = contract spec"),
    ("C247 voice type back to MaleRough", "main", m_voice_default, "NPC_ SP_SuperPeasant fields = contract spec"),
    ("C247 stealth meter flag cleared", "main", m_stealth_flag_cleared, "NPC_ SP_SuperPeasant fields = contract spec"),
    ("C247 PeasantAlias Is Companion", "main", m_alias_is_companion, "QUST SP_Main fields = contract spec"),
    ("C247 SP_Debug default 1", "main", m_debug_default_on, "GLOB SP_Debug fields = contract spec"),
    ("test VMAD value with the wrong master index", "test", m_test_master_index,
     "SuperPeasant:Test:Runner properties = contract"),
    ("CARRY menu button conditions removed", "main", m_mesg_cond_removed, "MESG SP_MenuMsg fields = contract spec"),
    ("CARRY 'Carry me' condition value 1", "main", m_mesg_cond_wrong, "MESG SP_MenuMsg fields = contract spec"),
]


def psc_tree_copy(dst):
    src = os.path.join(FO4W, "src", "papyrus")
    if os.path.isdir(dst):
        shutil.rmtree(dst)
    shutil.copytree(src, dst)
    return os.path.join(dst, "SuperPeasant")


def edit_file(path, fn):
    with open(path, encoding="utf-8", errors="replace") as f:
        t = f.read()
    with open(path, "w", encoding="utf-8") as f:
        f.write(fn(t))


PSC_TRICKY = r'''Scriptname SuperPeasant:Probe extends Quest Conditional
{ doc comment with Form Property Fake Auto inside }
;/ block comment
int Property Nope Auto
/;
GlobalVariable Property SP_Debug Auto Const Mandatory ; trailing comment Property X
SuperPeasant:Brain Property Brain Auto \
    Const Mandatory
float[] Property Speeds Auto Hidden
float Property TICK = 0.03 AutoReadOnly
string Property Text = "semi;colon Property Q Auto" AutoReadOnly Hidden
int Property Full Hidden
    int Function Get()
        Return 1
    EndFunction
EndProperty
Struct Pair
    float a = 1.0
    Form Property NotAProperty Auto
EndStruct
Group Tunables
    Actor Property Grouped Auto Const
EndGroup
'''


def main():
    T_ = Tally()
    if os.path.isdir(T):
        shutil.rmtree(T)
    os.makedirs(T)

    # ---------------- B1 determinism
    rc1, out1, m1, t1 = build_to(os.path.join(T, "a"))
    rc2, out2, m2, t2 = build_to(os.path.join(T, "b"))
    T_.check(rc1 == 0 and rc2 == 0, "B1 build exits 0", (out1 + out2)[-300:])
    same = rc1 == 0 and rc2 == 0 and open(m1, "rb").read() == open(m2, "rb").read() and \
        open(t1, "rb").read() == open(t2, "rb").read()
    T_.check(same, "B1 two builds are byte-identical")
    real = os.path.join(FO4W, "build", "Data", "SuperPeasant.esp")
    if os.path.isfile(real):
        T_.check(open(real, "rb").read() == open(m1, "rb").read(), "B1 build/Data/SuperPeasant.esp is current",
                 "rebuild with tools/esp/build_esp.py")

    # ---------------- B2 VMAD property kinds
    contract = json.load(open(B.DEFAULTS["contract"], encoding="utf-8"))
    import vanilla_index as vi
    ctx = B.Ctx(contract, vi.load(B.DEFAULTS["index"]), B.DEFAULT_ESM, {"P1": True, "P2": False})
    ctx.built = {r["edid"] for r in contract["records"] if r["priority"] == "P0"}
    plugin = contract["plugin"]["file"]
    cases = [
        ({"name": "A", "type": "GlobalVariable", "value": {"kind": "ours", "edid": "SP_Debug", "formid": "0x01000812"}},
         (vm.T_OBJECT, {"formid": 0x01000812, "alias": -1, "unused": 0})),
        ({"name": "B", "type": "ReferenceAlias", "value": {"kind": "alias", "quest": "SP_Main",
                                                           "quest_formid": "0x01000800", "alias_id": 1}},
         (vm.T_OBJECT, {"formid": 0x01000800, "alias": 1, "unused": 0})),
        ({"name": "C", "type": "ActorValue", "value": {"kind": "vanilla", "edid": "Health", "formid": "0x000002D4",
                                                       "type": "AVIF"}},
         (vm.T_OBJECT, {"formid": 0x2D4, "alias": -1, "unused": 0})),
        ({"name": "D", "type": "int", "value": {"value": 7}}, (vm.T_INT, 7)),
        ({"name": "E", "type": "float", "value": {"value": 0.5}}, (vm.T_FLOAT, 0.5)),
        ({"name": "F", "type": "bool", "value": {"value": True}}, (vm.T_BOOL, 1)),
        ({"name": "G", "type": "string", "value": {"value": "hi"}}, (vm.T_STRING, "hi")),
        ({"name": "H", "type": "float[]", "value": {"value": [1.0, 2.5]}}, (vm.T_ARRAY_BASE + vm.T_FLOAT, [1.0, 2.5])),
        ({"name": "I", "type": "Keyword[]", "value": [{"kind": "vanilla", "edid": "ActorTypeNPC",
                                                       "formid": "0x00013794", "type": "KYWD"}]},
         (vm.T_ARRAY_BASE + vm.T_OBJECT, [{"formid": 0x13794, "alias": -1, "unused": 0}])),
    ]
    props = []
    for p, (t, v) in cases:
        got = B.prop_vmad(ctx, p, plugin, "Probe")
        T_.check(got is not None and got["type"] == t and got["value"] == v, "B2 property kind %s %s" %
                 (p["type"], p["name"]), str(got))
        props.append(got)
    data = vm.vmad_bytes([vm.script("SuperPeasant:Probe", props)], "QUST",
                         [vm.quest_alias(0, [vm.script("SuperPeasant:PlayerAlias", props[:1])], 0x01000800)])
    T_.check(vm.encode(vm.decode(data, "QUST"), "QUST") == data, "B2 VMAD with every kind round-trips")
    fx = {"name": "SP_PunchFX", "type": "Explosion", "value": {"kind": "ours", "edid": "SP_PunchFX",
                                                               "formid": "0x0100081A"}}
    T_.check(B.prop_vmad(ctx, fx, plugin, "Probe") is None, "B2 unbuilt P2 target left out of the VMAD")

    # ---------------- B3 without P1/P2
    rc3, out3, m3, t3 = build_to(os.path.join(T, "nop"), ["--no-p1", "--no-p2"])
    rcv, fails3, _o = validate(m3, t3)
    p = er.Plugin.load(m3)
    npc = rec(p, "SP_SuperPeasant")
    # R4: SP_PerkImmortal is P0 (a Mandatory PeasantAlias property points at it), so --no-p1 still builds and lists it
    prkr = npc.get("PRKR")
    n_p0 = len([r for r in contract["records"] if r["plugin"] == plugin and r["priority"] == "P0"])
    T_.check(rc3 == 0 and rcv == 0 and prkr is not None and len(list(p.iter_records())) == n_p0,
             "B3 --no-p1 --no-p2 builds the %d P0 records (the P0 perk stays) and validates" % n_p0,
             "; ".join(fails3[:3]) or out3[-200:])
    # SNDR: vanilla subrecord order, one ANAM per contract file, loop only on the fist whoosh
    pm1 = er.Plugin.load(m1) if rc1 == 0 else None
    sndr = [r for r in pm1.iter_records() if r.sig == "SNDR"] if pm1 is not None else []
    want_sndr = {r["edid"]: r["spec"] for r in contract["records"] if r["type"] == "SNDR"}
    probs = []
    for r in sndr:
        sp = want_sndr.get(r.edid, {})
        sigs = [x.sig for x in r.subrecords]
        if sigs != ["EDID", "CNAM", "GNAM"] + ["ANAM"] * len(sp.get("files", [])) + ["ONAM", "LNAM", "BNAM"]:
            probs.append("%s order %s" % (r.edid, sigs))
        if r.get("LNAM").data[1] != (0x08 if sp.get("looping") == "Loop" else 0):
            probs.append("%s LNAM %s" % (r.edid, r.get("LNAM").data.hex()))
    T_.check(sorted(r.edid for r in sndr) == sorted(want_sndr) and not probs and
             [e for e in want_sndr if want_sndr[e].get("looping") == "Loop"] == ["SP_FistLoopSNDR"],
             "B3 %d SNDR records: vanilla order, one ANAM per file, only SP_FistLoopSNDR loops" % len(sndr),
             "; ".join(probs[:3]))
    # R4: the NPC owns the perk only with PRKR rank 1 (vanilla 2771/2771); rank 0 = HasPerk False in game
    pm = er.Plugin.load(m1) if rc1 == 0 else None
    ranks = [s.data[4] for s in rec(pm, "SP_SuperPeasant").get_all("PRKR")] if pm is not None else []
    T_.check(ranks == [1], "B3 NPC_ PRKR rank byte is 1", str(ranks))
    pe = vm.decode(rec(p, "SP_PunchEffect").get("VMAD").data, "MGEF")
    T_.check("SP_PunchFX" not in [x["name"] for x in pe["scripts"][0]["properties"]],
             "B3 SP_PunchFX property absent when the EXPL is not built")

    # ---------------- V1 fresh build validates
    rc, fails, out = validate(m1, t1)
    T_.check(rc == 0 and not fails, "V1 fresh build validates (0 FAIL)", "; ".join(fails[:4]))

    # ---------------- V2 plugin mutations
    for name, which, fn, want in MUTATIONS:
        d = os.path.join(T, "mut")
        os.makedirs(d, exist_ok=True)
        mm, tt = os.path.join(d, "SuperPeasant.esp"), os.path.join(d, "SuperPeasant_Test.esp")
        shutil.copyfile(m1, mm)
        shutil.copyfile(t1, tt)
        try:
            mutate(m1 if which == "main" else t1, mm if which == "main" else tt, fn)
        except Exception as e:  # noqa: BLE001
            T_.check(False, "V2 mutation %s applies" % name, repr(e))
            continue
        rc, fails, out = validate(mm, tt)
        hit = [f for f in fails if want in f]
        T_.check(rc == 1 and hit, "V2 caught: %s" % name, "wanted a FAIL containing %r; got %s" %
                 (want, fails[:4]))
    # HEDR count wrong (write without updating HEDR)
    p = er.Plugin.load(m1)
    h = p.header.get("HEDR")
    ver, n, nid = struct.unpack("<fiI", h.data)
    h.data = struct.pack("<fiI", ver, n + 1, nid)
    bad = os.path.join(T, "mut", "SuperPeasant.esp")
    with open(bad, "wb") as f:
        f.write(ew.plugin_bytes(p, update_hedr=False))
    rc, fails, out = validate(bad, t1)
    T_.check(any("HEDR record count" in f for f in fails), "V2 caught: HEDR record count", str(fails[:3]))

    # ---------------- V2 .psc mutations (copy of src/papyrus, --psc-root)
    root = os.path.join(T, "psc")
    sp = psc_tree_copy(root)
    ctrl = os.path.join(sp, "Controller.psc")
    rc, fails, out = validate(m1, t1, ["--psc-root", root])
    T_.check(rc == 0, "V2 unmodified .psc copy validates", "; ".join(fails[:3]))
    if os.path.isfile(ctrl):
        edit_file(ctrl, lambda t: t + "\nForm Property SP_Unlisted Auto\n")
        rc, fails, out = validate(m1, t1, ["--psc-root", root])
        T_.check(any("no other fillable non-hidden property" in f for f in fails),
                 "V2 caught: .psc declares a fillable property not in the contract", str(fails[:3]))
        psc_tree_copy(root)
        edit_file(ctrl, lambda t: t.replace("GlobalVariable Property SP_Debug Auto Const Mandatory",
                                            "GlobalVariable Property SP_Debug Auto Const", 1))
        rc, fails, out = validate(m1, t1, ["--psc-root", root])
        T_.check(any("type and flags" in f for f in fails), "V2 caught: .psc property lost Mandatory", str(fails[:3]))
        psc_tree_copy(root)
        edit_file(ctrl, lambda t: t.replace("GlobalVariable Property SP_Debug Auto Const Mandatory",
                                            "Form Property SP_Debug Auto Const Mandatory", 1))
        rc, fails, out = validate(m1, t1, ["--psc-root", root])
        T_.check(any("type and flags" in f for f in fails), "V2 caught: .psc property type differs", str(fails[:3]))
        psc_tree_copy(root)
        edit_file(ctrl, lambda t: t + "\nint Property ProbeHidden Auto Hidden\n")
        rc, fails, out = validate(m1, t1, ["--psc-root", root])
        T_.check(any(".pex property table" in f for f in fails) or
                 not os.path.isfile(os.path.join(FO4W, "build", "Data", "Scripts", "SuperPeasant", "Controller.pex")),
                 "V2 caught: .pex is stale (property missing in the compiled script)", str(fails[:3]))
        psc_tree_copy(root)
        os.remove(ctrl)
        rc, out = quiet(V.main, ["--main", m1, "--test", t1, "--psc-root", root, "-q"])
        T_.check(rc == 1 and "FAIL SuperPeasant:Controller (Controller.psc): .psc exists" in out,
                 "V2 caught: missing .psc (without --partial)")
        rc, out = quiet(V.main, ["--main", m1, "--test", t1, "--psc-root", root, "--partial", "-q"])
        T_.check("FAIL SuperPeasant:Controller (Controller.psc): .psc exists" not in out,
                 "V2 missing .psc is a NOTE with --partial")
    else:
        T_.check(True, "V2 .psc mutations skipped: Controller.psc not written yet")

    # ---------------- P1 psc parser
    ps = V.parse_psc(PSC_TRICKY)
    names = sorted(p["name"] for p in ps["properties"].values())
    T_.check(names == ["Brain", "Full", "Grouped", "SP_Debug", "Speeds", "TICK", "Text"], "P1 .psc parser finds the "
             "right properties", str(names))
    T_.check(ps["name"] == "SuperPeasant:Probe" and ps["extends"] == "Quest" and not ps["errors"],
             "P1 Scriptname / extends / no errors", str((ps["name"], ps["extends"], ps["errors"])))
    pr = ps["properties"]
    T_.check(pr["brain"]["flags"] == {"auto", "const", "mandatory"} and pr["brain"]["type"] == "SuperPeasant:Brain",
             "P1 continuation line joins flags", str(pr["brain"]))
    T_.check(pr["tick"]["readonly"] and pr["text"]["readonly"] and "hidden" in pr["text"]["flags"] and
             pr["full"].get("full") and pr["speeds"]["type"] == "float[]", "P1 AutoReadOnly / full / array kinds")

    # ---------------- P2 pex files
    pexdir = os.path.join(FO4W, "build", "Data", "Scripts", "SuperPeasant")
    n = 0
    for fn in sorted(os.listdir(pexdir)) if os.path.isdir(pexdir) else []:
        if not fn.endswith(".pex"):
            continue
        psc = os.path.join(FO4W, "src", "papyrus", "SuperPeasant", fn[:-4] + ".psc")
        try:
            px = V.parse_pex(open(os.path.join(pexdir, fn), "rb").read())
        except Exception as e:  # noqa: BLE001
            T_.check(False, "P2 %s parses" % fn, repr(e))
            continue
        n += 1
        if os.path.isfile(psc):
            pp = V.parse_psc(open(psc, encoding="utf-8", errors="replace").read())
            T_.check(set(px["properties"]) == set(pp["properties"]) or
                     os.path.getmtime(os.path.join(pexdir, fn)) < os.path.getmtime(psc),
                     "P2 %s properties = .psc (%d)" % (fn, len(px["properties"])),
                     str(set(px["properties"]) ^ set(pp["properties"])))
    T_.check(True, "P2 parsed %d .pex files" % n)

    shutil.rmtree(root, ignore_errors=True)          # no stray copy of src/papyrus left in work/
    print("RESULT pass=%d fail=%d" % (T_.p, T_.f))
    return 0 if T_.f == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
