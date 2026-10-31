"""Validate SuperPeasant.esp / SuperPeasant_Test.esp against src/contract.json (CONTRACT.md build step 6, agent B2).

Usage (stdlib only; run with -I -B; reads Fallout4.esm read-only, writes nothing except an optional --json report):
  python -I -B tools/esp/validate_esp.py [--main <esp>] [--test <esp>] [--contract <json>] [--esm <Fallout4.esm>]
        [--psc-root src/papyrus] [--build-data build/Data] [--pex-root build/Data/Scripts] [--partial] [--xedit]
        [--json out.json] [-q]
No arguments = the contract paths. Last line: RESULT pass=N fail=M. Exit 0 = no FAIL, 1 = FAIL(s), 2 = bad input.

Checks (each is one PASS/FAIL line; WARN/NOTE lines are informative and not counted):
  file       parses; byte-identical round trip (stored bytes AND every record decoded + re-encoded); TES4 version 131,
             not ESM/ESL/localized, HEDR 1.0, record count = records + groups, next object ID from the contract,
             masters as the contract; top groups unique, in vanilla order, holding only their own type
  records    exactly the contract's records (EDID, type, FormID); P1/P2 may be missing; own FormIDs in 0x800..0xFFF
             below the next object ID; record version 131, no VC info, not deleted
  strings    every localized-string field of every record is an inline, zero-terminated, printable string
  spec       each record's fields equal contract records[].spec (flags by name, enums, values, aliases, conditions,
             buttons, effects, entry points, models, armatures, factions, AI data ...)
  refs       every FormID in a known field (fo4_schema.FORMID_FIELDS, CTDA GetGlobalValue, VMAD objects, PLDT/PTDA
             references) resolves to a record of an allowed type in the plugin or its masters (Fallout4.esm scan,
             SuperPeasant.esp for the test plugin); VMAD alias values name an existing reference alias
  vmad       the VMAD of every record equals the contract scripts attached to it (script names, property names,
             Object values); nothing else is attached
  psc        every attached script's .psc exists, its Scriptname/extends match the contract, it declares every VMAD
             property with the contract type (case-insensitive) and the contract flags (Auto Const [Mandatory]),
             declares no other fillable non-hidden property, and every Mandatory property is filled; the filled form
             fits the declared type; the contract's Auto Hidden runtime properties are declared (WARN only)
  pex        when a compiled .pex exists: its property table equals the .psc (catches a stale build)
  paths      every model path in the plugins exists in build/Data or in the vanilla archives (A's path index);
             the contract NIF paths exist in build/Data
  sounds     SNDR fields = spec (vanilla subrecord order, Standard type, category, output model, ANAM list, looping,
             BNAM); every ANAM file is staged in build/Data, listed in paths.files_main, a clean 16-bit PCM WAV with the
             contract's channels/rate/frames and byte-identical (sha1) to the extracted clip; nothing else under
             build/Data/Sound
  xedit      (--xedit) helpers/xedit-headless/run_check.ps1 -Strict on both plugins reports 0 errors (about 40 s)
--partial: a contract script whose .psc does not exist yet is a NOTE instead of a FAIL (C/D still writing).
"""
import argparse
import gzip
import json
import mmap
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import esm_reader as er  # noqa: E402
import esp_writer as ew  # noqa: E402
import fo4_schema as S  # noqa: E402
import vmad as vm  # noqa: E402
from build_esp import PERK_ENTRY_POINTS, PERK_ENTRY_TABS  # noqa: E402  (one entry-point table with the builder)

# R4: the only PRKR rank vanilla NPC_ records use (Fallout4.esm 2771/2771). Independent of the builder on purpose.
PRKR_RANK_VANILLA = 1

FO4W = os.path.normpath(os.path.join(HERE, ".."))
DEFAULT_ESM = r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm"
TYPES_CACHE = os.path.join(FO4W, "work", "plugin", "esm_formid_types.json")
VANILLA_PATHS = os.path.join(FO4W, "work", "game_assets", "vanilla_paths.tsv.gz")
REC_HDR = struct.Struct("<4sIIIHHHH")


def hx(v):
    return "0x%08X" % (v & 0xFFFFFFFF)


# ------------------------------------------------------------------ results
class Results(object):
    def __init__(self, quiet=False):
        self.rows = []
        self.quiet = quiet

    def check(self, ok, name, detail=""):
        self.rows.append({"ok": bool(ok), "name": name, "detail": detail})
        if not self.quiet or not ok:
            print("%s %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""))
        return ok

    def warn(self, name, detail=""):
        self.rows.append({"ok": None, "name": name, "detail": detail, "level": "WARN"})
        print("WARN %s%s" % (name, (": " + detail) if detail else ""))

    def note(self, name, detail=""):
        self.rows.append({"ok": None, "name": name, "detail": detail, "level": "NOTE"})
        if not self.quiet:
            print("NOTE %s%s" % (name, (": " + detail) if detail else ""))

    @property
    def npass(self):
        return sum(1 for r in self.rows if r["ok"] is True)

    @property
    def nfail(self):
        return sum(1 for r in self.rows if r["ok"] is False)


# ------------------------------------------------------------------ master FormID -> signature
def scan_types(esm, wanted):
    """{formid: sig} for the wanted FormIDs (any depth) in a master file. Linear header walk; cached by size/mtime."""
    st = os.stat(esm)
    key = "%s|%d|%d" % (os.path.basename(esm).lower(), st.st_size, int(st.st_mtime))
    cache = {}
    try:
        with open(TYPES_CACHE, encoding="utf-8") as f:
            c = json.load(f)
        if c.get("key") == key:
            cache = {int(k, 16): v for k, v in c["types"].items()}
    except (OSError, ValueError, KeyError):
        pass
    need = {w for w in wanted if w not in cache}
    if need:
        found = {}
        with open(esm, "rb") as f:
            mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
            try:
                n = len(mm)
                pos = 24 + struct.unpack_from("<I", mm, 4)[0]
                unpack = REC_HDR.unpack_from
                while pos < n:
                    sig, size, _fl, fid, _a, _b, _c, _d = unpack(mm, pos)
                    if sig == b"GRUP":
                        pos += 24
                        continue
                    if fid in need:
                        found[fid] = sig.decode("latin-1")
                        if len(found) == len(need):
                            break
                    pos += 24 + size
            finally:
                mm.close()
        for w in need:
            cache[w] = found.get(w)          # None = not in the file (cached too)
        try:
            os.makedirs(os.path.dirname(TYPES_CACHE), exist_ok=True)
            with open(TYPES_CACHE, "w", encoding="utf-8") as f:
                json.dump({"key": key, "types": {hx(k): v for k, v in cache.items()}}, f)
        except OSError:
            pass
    return {w: cache.get(w) for w in wanted}


# ------------------------------------------------------------------ .psc parsing
PSC_PROP = re.compile(r"^(?P<type>[A-Za-z_][\w:]*(?:\[\])?)\s+property\s+(?P<name>[A-Za-z_]\w*)(?P<rest>.*)$", re.I)
PSC_SCRIPTNAME = re.compile(r"^scriptname\s+(?P<name>[\w:]+)(?:\s+extends\s+(?P<ext>[\w:]+))?(?P<flags>.*)$", re.I)
PROP_FLAGS = ("auto", "autoreadonly", "const", "mandatory", "hidden", "conditional")


def strip_psc(text):
    """Remove ; line comments, ;/ block /; comments and { doc } comments (string-aware); join '\\' continuations."""
    out, i, n = [], 0, len(text)
    in_str = False
    while i < n:
        ch = text[i]
        if in_str:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(text[i + 1])
                i += 2
                continue
            if ch == '"' or ch == "\n":
                in_str = False
            i += 1
            continue
        if ch == '"':
            in_str = True
            out.append(ch)
            i += 1
        elif text.startswith(";/", i):
            j = text.find("/;", i + 2)
            seg = text[i:(j + 2) if j >= 0 else n]
            out.append("\n" * seg.count("\n"))
            i = (j + 2) if j >= 0 else n
        elif ch == ";":
            j = text.find("\n", i)
            i = j if j >= 0 else n
        elif ch == "{":
            j = text.find("}", i + 1)
            seg = text[i:(j + 1) if j >= 0 else n]
            out.append("\n" * seg.count("\n"))
            i = (j + 1) if j >= 0 else n
        else:
            out.append(ch)
            i += 1
    s = "".join(out).replace("\r\n", "\n")
    s = re.sub(r"\\[ \t]*\n", " ", s)
    return s


def parse_psc(text):
    """-> {"name", "extends", "script_flags", "properties": {lowername: {...}}, "errors": [...]}"""
    res = {"name": None, "extends": None, "script_flags": [], "properties": {}, "errors": []}
    lines = strip_psc(text).split("\n")
    in_full, in_struct = None, False
    for ln, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line:
            continue
        low = line.lower()
        if in_full is not None:
            if low.startswith("endproperty"):
                in_full = None
            continue
        if in_struct:
            if low.startswith("endstruct"):
                in_struct = False
            continue
        if low.startswith("struct "):
            in_struct = True
            continue
        m = PSC_SCRIPTNAME.match(line)
        if m and res["name"] is None:
            res["name"] = m.group("name")
            res["extends"] = m.group("ext")
            res["script_flags"] = [w.lower() for w in m.group("flags").split()]
            continue
        m = PSC_PROP.match(line)
        if not m:
            continue
        rest = m.group("rest")
        default = None
        dm = re.match(r'\s*=\s*("(?:[^"\\]|\\.)*"|[^\s]+)(.*)$', rest)
        if dm:
            default, rest = dm.group(1), dm.group(2)
        words = [w.lower() for w in rest.split()]
        bad = [w for w in words if w not in PROP_FLAGS]
        if bad:
            res["errors"].append("line %d: unknown property flag(s) %s" % (ln, bad))
        p = {"name": m.group("name"), "type": m.group("type"), "flags": set(words), "default": default, "line": ln,
             "auto": "auto" in words, "readonly": "autoreadonly" in words}
        if not p["auto"] and not p["readonly"]:
            p["full"] = True
            in_full = p["name"]
        key = p["name"].lower()
        if key in res["properties"]:
            res["errors"].append("line %d: property %s declared twice" % (ln, p["name"]))
        res["properties"][key] = p
    if res["name"] is None:
        res["errors"].append("no Scriptname line")
    return res


def contract_flags(flag_text):
    return set(w.lower() for w in flag_text.split())


# ------------------------------------------------------------------ .pex parsing (Fallout 4, little endian)
class _P(object):
    def __init__(self, d):
        self.d, self.i = d, 0

    def u(self, fmt):
        v = struct.unpack_from("<" + fmt, self.d, self.i)
        self.i += struct.calcsize("<" + fmt)
        return v[0] if len(v) == 1 else v

    def wstr(self):
        n = self.u("H")
        s = self.d[self.i:self.i + n].decode("cp1252", errors="replace")
        self.i += n
        return s


def _pex_skip_vardata(p):
    t = p.u("B")
    if t in (1, 2):
        p.u("H")
    elif t in (3, 4):
        p.u("I")
    elif t == 5:
        p.u("B")
    elif t != 0:
        raise ValueError("pex: unknown variable data type %d" % t)


# Papyrus opcodes (Fallout 4): fixed argument count; +varargs = an Int variable-data count, then that many args
PEX_OPS = {0x00: 0, 0x01: 3, 0x02: 3, 0x03: 3, 0x04: 3, 0x05: 3, 0x06: 3, 0x07: 3, 0x08: 3, 0x09: 3, 0x0A: 2,
           0x0B: 2, 0x0C: 2, 0x0D: 2, 0x0E: 2, 0x0F: 3, 0x10: 3, 0x11: 3, 0x12: 3, 0x13: 3, 0x14: 1, 0x15: 2,
           0x16: 2, 0x17: 3, 0x18: 2, 0x19: 3, 0x1A: 1, 0x1B: 3, 0x1C: 3, 0x1D: 3, 0x1E: 2, 0x1F: 2, 0x20: 3,
           0x21: 3, 0x22: 4, 0x23: 4, 0x24: 3, 0x25: 1, 0x26: 3, 0x27: 3, 0x28: 5, 0x29: 5, 0x2A: 3, 0x2B: 3,
           0x2C: 1, 0x2D: 3, 0x2E: 1, 0x2F: 6}
PEX_VARARGS = {0x17, 0x18, 0x19}


def _pex_vardata(p):
    t = p.u("B")
    if t in (1, 2):
        return t, p.u("H")
    if t == 3:
        return t, p.u("i")
    if t == 4:
        return t, p.u("f")
    if t == 5:
        return t, p.u("B")
    if t != 0:
        raise ValueError("pex: unknown variable data type %d" % t)
    return t, None


def _pex_skip_function(p):
    p.u("HHIB")                                      # return type, docstring, user flags, flags
    for _ in range(2):                               # params, then locals: (name, type) pairs
        n = p.u("H")                                 # (not "p.i += 4 * p.u()": p.i would be read first)
        p.i += 4 * n
    for _ in range(p.u("H")):
        op = p.u("B")
        if op not in PEX_OPS:
            raise ValueError("pex: unknown opcode 0x%02X" % op)
        for _ in range(PEX_OPS[op]):
            _pex_vardata(p)
        if op in PEX_VARARGS:
            t, n = _pex_vardata(p)
            if t != 3:
                raise ValueError("pex: varargs count is not an int")
            for _ in range(n):
                _pex_vardata(p)


def parse_pex(data):
    """-> {"script": name, "extends": parent, "user_flags": {lowername: bit}, "properties": {lower: {name, type,
    user_flags, auto, const}}, "partial": bool}. Full/read-only properties have their handler code skipped."""
    p = _P(data)
    magic, major, minor, game = p.u("IBBH")
    if magic != 0xFA57C0DE or game != 2:
        raise ValueError("not a Fallout 4 .pex (magic %08X game %d)" % (magic, game))
    p.u("Q")
    p.wstr(); p.wstr(); p.wstr()                     # noqa: E702  source, user, machine
    strings = [p.wstr() for _ in range(p.u("H"))]
    if p.u("B"):
        p.u("Q")
        for _ in range(p.u("H")):
            p.u("HHHB")
            n = p.u("H")
            p.i += 2 * n
        for _ in range(p.u("H")):                    # property groups
            p.u("HHHI")
            n = p.u("H")
            p.i += 2 * n
        for _ in range(p.u("H")):                    # struct orders
            p.u("HH")
            n = p.u("H")
            p.i += 2 * n
    uflags = {}
    for _ in range(p.u("H")):
        ni, bit = p.u("HB")
        uflags[strings[ni].lower()] = bit            # Caprica writes 'mandatory', the CK 'Mandatory'
    nobj = p.u("H")
    if nobj < 1:
        raise ValueError("pex has no object")
    name_i, _size = p.u("HI")
    parent_i, _doc, _const, _uf, _auto = p.u("HHBIH")
    out = {"script": strings[name_i], "extends": strings[parent_i] or None, "user_flags": uflags,
           "properties": {}, "partial": False}
    for _ in range(p.u("H")):                        # structs
        p.u("H")
        for _ in range(p.u("H")):
            p.u("HHI")
            _pex_skip_vardata(p)
            p.u("BH")
    variables = {}
    for _ in range(p.u("H")):
        vn, vt, _vuf = p.u("HHI")
        _pex_skip_vardata(p)
        const = p.u("B")
        variables[strings[vn]] = {"type": strings[vt], "const": bool(const)}
    for _ in range(p.u("H")):
        pn, pt, _pd, puf, pfl = p.u("HHHIB")
        prop = {"name": strings[pn], "type": strings[pt], "user_flags": puf, "auto": bool(pfl & 4)}
        if pfl & 4:
            var = strings[p.u("H")]
            prop["const"] = variables.get(var, {}).get("const", False)
        else:
            prop["const"] = None
            prop["readonly"] = bool(pfl & 1) and not pfl & 2
            if pfl & 1:
                _pex_skip_function(p)                # read handler
            if pfl & 2:
                _pex_skip_function(p)                # write handler
        out["properties"][prop["name"].lower()] = prop
    return out


# ------------------------------------------------------------------ plugin model
class Loaded(object):
    def __init__(self, path, meta):
        self.path = path
        self.meta = meta
        self.raw = open(path, "rb").read()
        self.plugin = er.Plugin.from_bytes(self.raw)
        self.records = list(self.plugin.iter_records())
        self.by_fid = {r.formid: r for r in self.records}
        self.by_edid = {r.edid: r for r in self.records}
        self.masters = self.plugin.masters

    def own_index(self):
        return len(self.masters)


def lstring_ok(data):
    if not data or data[-1:] != b"\0":
        return False
    body = data[:-1]
    return b"\0" not in body and all(32 <= b < 127 for b in body)


# ------------------------------------------------------------------ main validator
class Validator(object):
    def __init__(self, a, contract, R):
        self.a = a
        self.c = contract
        self.R = R
        self.records_by_edid = {r["edid"]: r for r in contract["records"]}
        self.esm_types = {}
        self.main = None
        self.test = None

    # ---------- resolution
    def file_record_type(self, plug, fid):
        """(sig or None, where) of FormID `fid` as written in plugin `plug`."""
        if fid in S.ENGINE_FORMS:
            return S.ENGINE_FORMS[fid], "engine"
        mi = fid >> 24
        local = fid & 0xFFFFFF
        if mi == plug.own_index():
            r = plug.by_fid.get(fid)
            return (r.sig if r else None), os.path.basename(plug.path)
        if mi > plug.own_index():
            return None, "bad master index %d" % mi
        mname = plug.masters[mi]
        if mname.lower() == "fallout4.esm":
            return self.esm_types.get(local), mname
        if self.main is not None and mname.lower() == os.path.basename(self.main.path).lower():
            r = self.main.by_fid.get((self.main.own_index() << 24) | local)
            return (r.sig if r else None), mname
        return None, "master %s not loaded" % mname

    def record_of(self, plug, fid):
        mi = fid >> 24
        if mi == plug.own_index():
            return plug.by_fid.get(fid)
        if self.main is not None and plug is not self.main and mi < len(plug.masters) and \
                plug.masters[mi].lower() == os.path.basename(self.main.path).lower():
            return self.main.by_fid.get((self.main.own_index() << 24) | (fid & 0xFFFFFF))
        return None

    def expected_fid(self, plug, edid):
        """FormID a contract record (ours) has in the numbering of plugin plug."""
        r = self.records_by_edid[edid]
        if r["plugin"] == os.path.basename(plug.path):
            mi = plug.own_index()
        else:
            mi = [m.lower() for m in plug.masters].index(r["plugin"].lower())
        return (mi << 24) | int(r["local_id"], 16)

    def ref_fid(self, plug, name):
        """FormID (in plug's numbering) of an EditorID that is ours or vanilla (contract tables)."""
        if name in self.records_by_edid:
            return self.expected_fid(plug, name)
        v = self.c["vanilla"].get(name)
        if v is None:
            raise KeyError(name)
        return int(v["formid"], 16)

    # ---------- collect FormIDs for the master scan
    def collect_fids(self, plug):
        out = set()
        for r in plug.records:
            for sig, fid, _allowed, _where in self.iter_formids(r):
                if fid and fid >> 24 == 0:
                    out.add(fid & 0xFFFFFF)
        return out

    def iter_formids(self, rec):
        """Yield (sig, formid, allowed sigs or None, where) for every FormID in a known field of rec."""
        fields = S.FORMID_FIELDS.get(rec.sig, {})
        for i, s in enumerate(rec.subrecords):
            if s.sig == "MODL" and rec.sig not in S.MODEL_PATH_FORMID_TYPES:
                continue
            for off, stride, allowed in fields.get(s.sig, []):
                if stride:
                    for o in range(off, len(s.data) - 3, stride):
                        yield s.sig, struct.unpack_from("<I", s.data, o)[0], allowed, "%s[%d]" % (s.sig, o)
                elif len(s.data) >= off + 4:
                    yield s.sig, struct.unpack_from("<I", s.data, off)[0], allowed, "%s@%d" % (s.sig, off)
            if s.sig == "CTDA" and len(s.data) == 32:
                c = S.ctda_decode(s.data)
                if c["function"] == S.FUNC_GET_GLOBAL_VALUE:
                    yield s.sig, c["param1"], {"GLOB"}, "CTDA GetGlobalValue param1"
                if c["reference"]:
                    yield s.sig, c["reference"], None, "CTDA reference"
            if rec.sig == "PACK" and s.sig == "PLDT":
                t, v, _r, _ci = S.PLDT.unpack(s.data)
                if t == 0:
                    yield s.sig, v, S.REF_SIGS, "PLDT near reference"
                elif t == 6:
                    yield s.sig, v, {"KYWD"}, "PLDT linked ref keyword"
            if rec.sig == "PACK" and s.sig == "PTDA" and len(s.data) >= 8:
                t, v = struct.unpack_from("<iI", s.data)
                if t == 0:
                    yield s.sig, v, S.REF_SIGS, "PTDA specific reference"
                elif t == 1:
                    yield s.sig, v, None, "PTDA object id"
            if s.sig == "VMAD":
                try:
                    d = vm.decode(s.data, rec.sig)
                except vm.VMADError:
                    continue
                for obj in vmad_objects(d):
                    yield "VMAD", obj["formid"], None, "VMAD object"

    # ---------- per plugin
    def check_plugin(self, plug, meta, is_test):
        R = self.R
        name = os.path.basename(plug.path)
        tag = name + ": "
        # round trip, raw then fully decoded
        R.check(ew.plugin_bytes(er.Plugin.from_bytes(plug.raw), update_hedr=False) == plug.raw,
                tag + "byte-identical round trip (stored bytes)")
        p2 = er.Plugin.from_bytes(plug.raw)
        for r in p2.iter_records():
            r.subrecords  # noqa: B018  decode every record
        p2.header.subrecords  # noqa: B018
        R.check(ew.plugin_bytes(p2, update_hedr=False) == plug.raw, tag + "byte-identical round trip (decoded)")
        # header
        h = plug.plugin.header
        ver, nrec, nid = plug.plugin.hedr
        nrg = ew.count_records_and_groups(plug.plugin.groups)
        R.check(h.version == 131 and h.vc1 == 0 and h.vc2 == 0, tag + "TES4 form version 131",
                "version %d" % h.version)
        R.check(not h.flags & (er.FLAG_ESM | er.FLAG_ESL | er.FLAG_LOCALIZED),
                tag + "not ESM / ESL / localized", "flags %08X" % h.flags)
        R.check(abs(ver - meta["hedr_version"]) < 1e-6, tag + "HEDR version %.1f" % meta["hedr_version"], str(ver))
        R.check(nrec == nrg, tag + "HEDR record count = records + groups", "%d vs %d" % (nrec, nrg))
        want_nid = int(meta["next_object_id"], 16)
        R.check(nid == want_nid, tag + "HEDR next object ID %s" % hx(want_nid), hx(nid))
        R.check(plug.masters == meta["masters"], tag + "masters %s" % meta["masters"], str(plug.masters))
        R.check(all(len(s.data) == 8 for s in h.subrecords if s.sig == "DATA"), tag + "MAST DATA 8 bytes each")
        # groups
        labels = [g.label_sig for g in plug.plugin.groups]
        R.check(len(labels) == len(set(labels)), tag + "no duplicated top group", str(labels))
        order = [ew.TOP_ORDER.index(l) if l in ew.TOP_ORDER else 999 for l in labels]
        R.check(order == sorted(order), tag + "top groups in Fallout4.esm order", " ".join(labels))
        bad = [(g.label_sig, c.sig) for g in plug.plugin.groups for c in g.children
               if not isinstance(c, er.Record) or c.sig != g.label_sig or g.gtype != 0]
        R.check(not bad, tag + "every top group holds only its own record type", str(bad[:5]))
        # record set
        want = [r for r in self.c["records"] if r["plugin"] == name]
        have = {r.edid: r for r in plug.records}
        problems = []
        for w in want:
            r = have.get(w["edid"])
            if r is None:
                if w["priority"] == "P0":
                    problems.append("missing P0 %s" % w["edid"])
                else:
                    R.note(tag + "optional %s (%s) not built" % (w["edid"], w["priority"]))
                continue
            if r.sig != w["type"]:
                problems.append("%s is %s, contract %s" % (w["edid"], r.sig, w["type"]))
            if r.formid != int(w["file_formid"], 16):
                problems.append("%s FormID %s, contract %s" % (w["edid"], hx(r.formid), w["file_formid"]))
        extra = [e for e in have if e not in {w["edid"] for w in want}]
        if extra:
            problems.append("records not in the contract: %s" % extra)
        R.check(not problems, tag + "records = contract (%d)" % len(want), "; ".join(problems))
        fids = [r.formid for r in plug.records]
        edids = [r.edid for r in plug.records]
        R.check(len(fids) == len(set(fids)) and len(edids) == len(set(edids)), tag + "FormIDs and EditorIDs unique")
        lo, hi = (int(x, 16) for x in meta["local_id_range"])
        badid = [hx(f) for f in fids if f >> 24 != plug.own_index() or not lo <= (f & 0xFFFFFF) <= hi
                 or (f & 0xFFFFFF) >= want_nid]
        R.check(not badid, tag + "own FormIDs: master index %d, local %s..%s, below next ID" %
                (plug.own_index(), meta["local_id_range"][0], meta["local_id_range"][1]), str(badid))
        prefix = "SPTest_" if is_test else "SP_"
        # R6 step 6: a DFOB carries the engine's own default-object name (e.g. NoPathFailureWarping_DO), never our prefix
        dfob = {r.edid for r in plug.records if r.sig == "DFOB" and r.edid and r.edid.endswith("_DO")}
        R.check(all(e and (e.startswith(prefix) or e in dfob) for e in edids), tag + "EditorIDs start with %s (DFOB: the engine's *_DO name)" % prefix)
        bad = [r.edid for r in plug.records if r.version != 131 or r.vc1 or r.vc2 or r.is_deleted]
        R.check(not bad, tag + "record headers: version 131, no VC info, not deleted", str(bad))
        # strings
        bad = []
        for r in plug.records:
            for s in r.subrecords:
                if s.sig in S.LSTRING_SIGS.get(r.sig, []) and not lstring_ok(s.data):
                    bad.append("%s %s %r" % (r.edid, s.sig, s.data[:12]))
            ed = r.get("EDID")
            if ed is None or not lstring_ok(ed.data):
                bad.append("%s EDID" % r.edid)
        R.check(not bad, tag + "all EDID/name/text fields are inline printable strings", "; ".join(bad[:6]))
        # references
        self.check_refs(plug, tag)
        # spec
        for w in want:
            r = have.get(w["edid"])
            if r is not None:
                self.check_spec(plug, r, w, tag)
        # vmad vs contract
        for w in want:
            r = have.get(w["edid"])
            if r is not None:
                self.check_vmad(plug, r, tag)
        # model paths
        self.check_paths(plug, tag)
        # sound files named by SNDR records
        self.check_sounds(plug, tag)

    # ---------- references
    def check_refs(self, plug, tag):
        bad, n = [], 0
        for r in plug.records:
            for sig, fid, allowed, where in self.iter_formids(r):
                if fid == 0:
                    if sig in ("ALFR", "ALPC", "EFID", "LNAM", "RNAM", "DOFT", "SNAM", "PRKR", "PKCU") or \
                            (r.sig == "ARMO" and sig == "MODL") or (r.sig == "OTFT" and sig == "INAM"):
                        bad.append("%s %s is NULL" % (r.edid, where))
                    continue
                n += 1
                t, src = self.file_record_type(plug, fid)
                if t is None:
                    bad.append("%s %s %s unresolved (%s)" % (r.edid, where, hx(fid), src))
                elif allowed is not None and t not in allowed:
                    bad.append("%s %s %s is %s, allowed %s" % (r.edid, where, hx(fid), t, sorted(allowed)))
            v = r.get("VMAD")
            if v is not None:
                for obj in vmad_objects(vm.decode(v.data, r.sig)):
                    if obj["alias"] >= 0:
                        q = self.record_of(plug, obj["formid"])
                        ids = alias_ids(q) if q is not None and q.sig == "QUST" else {}
                        if obj["alias"] not in ids:
                            bad.append("%s VMAD alias %d of %s not found" % (r.edid, obj["alias"], hx(obj["formid"])))
        self.R.check(not bad, tag + "every FormID in a known field resolves to an allowed type (%d checked)" % n,
                     "; ".join(bad[:8]))

    # ---------- VMAD vs contract
    def contract_scripts_for(self, edid):
        out = []
        for name, s in self.c["scripts"].items():
            at = s.get("attached_to") or {}
            if at.get("record") == edid:
                m = re.match(r"alias (\d+)", at.get("slot", ""))
                out.append((int(m.group(1)) if m else None, name, s))
        return out

    def expected_value(self, plug, prop):
        """(formid, alias) or None (left out: unbuilt optional target)."""
        val = prop["value"]
        k = val.get("kind")
        if k == "alias":
            return self.expected_fid(plug, val["quest"]), int(val["alias_id"])
        if k == "ours":
            ed = val["edid"]
            fid = self.expected_fid(plug, ed)
            if self.record_of(plug, fid) is None:
                return None
            return fid, -1
        if k == "vanilla":
            return int(val["formid"], 16), -1
        raise ValueError("unknown value kind %r" % k)

    def check_vmad(self, plug, r, tag):
        R = self.R
        want = self.contract_scripts_for(r.edid)
        v = r.get("VMAD")
        if not want:
            R.check(v is None, tag + "%s has no VMAD (no contract script)" % r.edid)
            return
        if not R.check(v is not None, tag + "%s has a VMAD" % r.edid):
            return
        d = vm.decode(v.data, r.sig)
        got = {}
        for s in d["scripts"]:
            got[(None, s["name"])] = s
        fr = d.get("fragments")
        if fr:
            for a in fr.get("aliases", []):
                if a["object"]["formid"] != r.formid:
                    R.check(False, tag + "%s VMAD alias %d object is the quest itself" % (r.edid,
                                                                                            a["object"]["alias"]))
                for s in a["scripts"]:
                    got[(a["object"]["alias"], s["name"])] = s
            R.check(not fr.get("fragments") and fr["script"]["name"] == "", tag + "%s VMAD has no stage fragments" %
                    r.edid)
        R.check(d["version"] == 6 and d["obj_format"] == 2, tag + "%s VMAD version 6 / object format 2" % r.edid)
        exp_keys = {(a, n) for a, n, _s in want}
        R.check(set(got) == exp_keys, tag + "%s scripts = contract" % r.edid,
                "have %s, want %s" % (sorted(map(str, got)), sorted(map(str, exp_keys))))
        for alias_id, name, s in want:
            have = got.get((alias_id, name))
            if have is None:
                continue
            problems = []
            hp = {p["name"]: p for p in have["properties"]}
            if have["flags"] != 0:
                problems.append("script flags %d" % have["flags"])
            wanted_names = set()
            for prop in s.get("properties", []):
                ev = self.expected_value(plug, prop)
                if ev is None:
                    if prop["name"] in hp:
                        problems.append("%s filled although its target is not built" % prop["name"])
                    elif "mandatory" in contract_flags(prop["flags"]):
                        problems.append("Mandatory %s has no built target" % prop["name"])
                    continue
                wanted_names.add(prop["name"])
                p = hp.get(prop["name"])
                if p is None:
                    problems.append("missing %s" % prop["name"])
                    continue
                if p["type"] != vm.T_OBJECT or p["flags"] != vm.FLAG_EDITED:
                    problems.append("%s type %d flags %d" % (prop["name"], p["type"], p["flags"]))
                    continue
                if (p["value"]["formid"], p["value"]["alias"]) != ev:
                    problems.append("%s = (%s, %d), want (%s, %d)" % (prop["name"], hx(p["value"]["formid"]),
                                                                       p["value"]["alias"], hx(ev[0]), ev[1]))
            extra = [n for n in hp if n not in wanted_names]
            if extra:
                problems.append("not in the contract: %s" % extra)
            R.check(not problems, tag + "%s%s %s properties = contract (%d)" %
                    (r.edid, "" if alias_id is None else " alias %d" % alias_id, name, len(wanted_names)),
                    "; ".join(problems))

    # ---------- spec checks
    def check_spec(self, plug, r, w, tag):
        sp = w.get("spec", {})
        bad = []
        f = getattr(self, "spec_" + r.sig.rstrip("_").lower(), None)
        if f is None:
            return
        try:
            f(plug, r, sp, bad)
        except (KeyError, ValueError, struct.error, TypeError, AttributeError) as e:
            bad.append("could not check: %r" % (e,))
        self.R.check(not bad, tag + "%s %s fields = contract spec" % (r.sig, r.edid), "; ".join(bad))

    def _str(self, r, sig):
        s = r.get(sig)
        return s.zstring() if s is not None else None

    def spec_qust(self, plug, r, sp, bad):
        fl = struct.unpack_from("<H", r.get("DNAM").data)[0]
        for n in sp.get("flags", []):
            if not fl & S.QUST_FLAGS[n]:
                bad.append("flag %s not set" % n)
        for n in sp.get("not_flags", []):
            if fl & S.QUST_FLAGS[n]:
                bad.append("flag %s set" % n)
        if "priority" in sp and r.get("DNAM").data[S.QUST_DNAM_PRIORITY_OFS] != sp["priority"]:
            bad.append("priority %d" % r.get("DNAM").data[2])
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))
        for sig in S.QUST_STAGE_OBJECTIVE_SIGS + ("CTDA",):
            if r.get(sig) is not None and sig != "CNAM":
                bad.append("has %s" % sig)
        aliases = parse_aliases(r)
        want = sp.get("aliases", [])
        if [a["id"] for a in aliases] != [a["id"] for a in want]:
            bad.append("alias ids %s" % [a["id"] for a in aliases])
        anam = r.get("ANAM")
        nxt = sp.get("next_alias_id", (max([a["id"] for a in want]) + 1) if want else 0)
        if anam is None or struct.unpack("<I", anam.data)[0] != nxt:
            bad.append("ANAM next alias id != %d" % nxt)
        for a, wa in zip(aliases, want):
            if a.get("ALID") != wa["name"]:
                bad.append("alias %d name %r" % (a["id"], a.get("ALID")))
            wf = 0
            for n in wa.get("flags", []):
                wf |= S.ALIAS_FLAGS[n]
            if a.get("FNAM") != wf:
                bad.append("alias %d flags %s != %s" % (a["id"], hex(a.get("FNAM") or 0), hex(wf)))
            fill = wa.get("fill", "").lower()
            if fill.startswith("forced reference"):
                m = re.search(r"0x[0-9A-Fa-f]+", wa["fill"])
                if a.get("ALFR") != int(m.group(0), 16):
                    bad.append("alias %d ALFR %s" % (a["id"], a.get("ALFR")))
            elif any(k in a for k in S.ALIAS_FILL_SIGS):
                bad.append("alias %d has a fill %s" % (a["id"], [k for k in S.ALIAS_FILL_SIGS if k in a]))
            want_pk = [self.ref_fid(plug, p) for p in wa.get("packages", [])]
            if a.get("ALPC", []) != want_pk:
                bad.append("alias %d packages %s" % (a["id"], [hx(x) for x in a.get("ALPC", [])]))
            if a["seq"][-1] != "ALED":
                bad.append("alias %d not closed by ALED" % a["id"])

    def spec_npc(self, plug, r, sp, bad):
        acbs = r.get("ACBS").data
        fl = struct.unpack_from("<I", acbs)[0]
        for n in sp.get("acbs_flags_set", []):
            if not fl & S.ACBS_FLAGS[n]:
                bad.append("ACBS %s not set" % n)
        for n in sp.get("acbs_flags_clear", []):
            if fl & S.ACBS_FLAGS[n]:
                bad.append("ACBS %s set" % n)
        if "level" in sp and struct.unpack_from("<h", acbs, S.ACBS_LEVEL_OFS)[0] != sp["level"]:
            bad.append("level %d" % struct.unpack_from("<h", acbs, 6)[0])
        if struct.unpack_from("<H", acbs, S.ACBS_TEMPLATE_FLAGS_OFS)[0] != 0:
            bad.append("template flags set without TPLT")
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))
        facs = [struct.unpack("<Ib", s.data) for s in r.get_all("SNAM")]
        want = [(self.ref_fid(plug, f["faction"]), f["rank"]) for f in sp.get("factions", [])]
        if facs != want:
            bad.append("factions %s" % facs)
        if struct.unpack("<I", r.get("RNAM").data)[0] != self.ref_fid(plug, sp["race"]):
            bad.append("race")
        if sp.get("default_outfit") and struct.unpack("<I", r.get("DOFT").data)[0] != \
                self.ref_fid(plug, sp["default_outfit"]):
            bad.append("default outfit")
        aidt = r.get("AIDT").data
        ai = sp.get("ai_data", {})
        for k, table in (("aggression", S.AIDT_AGGRESSION), ("confidence", S.AIDT_CONFIDENCE),
                         ("assistance", S.AIDT_ASSISTANCE)):
            if k in ai and aidt[S.AIDT_OFS[k]] != table[ai[k]]:
                bad.append("AI %s = %d" % (k, aidt[S.AIDT_OFS[k]]))
        for sig in ("CNTO", "COCT", "PKID", "VMAD", "TPLT", "TPTA", "EITM"):
            if r.get(sig) is not None:
                bad.append("has %s" % sig)
        if sp.get("voice_type"):
            # C247 E1: exactly one VTCK, = the contract voice type (EMPTY 0x001091E3: no default dialogue)
            vts = [struct.unpack("<I", s.data)[0] for s in r.get_all("VTCK") if len(s.data) == 4]
            if vts != [self.ref_fid(plug, sp["voice_type"])]:
                bad.append("VTCK %s != %s" % ([hx(x) for x in vts], sp["voice_type"]))
        perks =[struct.unpack_from("<I", s.data)[0] for s in r.get_all("PRKR")]
        # R4: every PRKR is 5 bytes with rank 1 (Fallout4.esm: 2771/2771). Rank 0 = perk not owned (HasPerk False).
        bad_rank = [s.data.hex() for s in r.get_all("PRKR") if len(s.data) != 5 or s.data[4] != PRKR_RANK_VANILLA]
        if bad_rank:
            bad.append("PRKR rank != 1 %s (vanilla 2771/2771 use rank 1; 0 = perk not owned)" % bad_rank)
        cnt = r.get("PRKZ")
        if (cnt is None and perks) or (cnt is not None and struct.unpack("<I", cnt.data)[0] != len(perks)):
            bad.append("PRKZ count")
        want_perks = []
        for p in sp.get("perks", []):
            ed = p.split(" ")[0]
            fid = self.expected_fid(plug, ed)
            if self.record_of(plug, fid) is not None:
                want_perks.append(fid)
        if perks != want_perks:
            bad.append("perks %s != %s" % ([hx(x) for x in perks], [hx(x) for x in want_perks]))

    def spec_arma(self, plug, r, sp, bad):
        if struct.unpack("<I", r.get("BOD2").data)[0] != int(sp["bod2_value"], 16):
            bad.append("BOD2 %s" % r.get("BOD2").data.hex())
        slots = sum(1 << (s - 30) for s in sp.get("bod2_slots", []))
        if sp.get("bod2_slots") and slots != int(sp["bod2_value"], 16):
            bad.append("contract bod2_slots/bod2_value disagree")
        if struct.unpack("<I", r.get("RNAM").data)[0] != self.ref_fid(plug, sp["race"]):
            bad.append("race")
        if self._str(r, "MOD2") != sp["male_model"]:
            bad.append("MOD2 %r" % self._str(r, "MOD2"))
        if sp.get("female_model") and self._str(r, "MOD3") != sp["female_model"]:
            bad.append("MOD3 %r" % self._str(r, "MOD3"))
        for sig in ("MO2T", "MO3T", "MO2S", "MO3S", "MOD4", "MOD5"):
            if r.get(sig) is not None:
                bad.append("has %s" % sig)
        races = [struct.unpack("<I", s.data)[0] for s in r.get_all("MODL")]
        if races != [self.ref_fid(plug, x) for x in sp.get("additional_races", [])]:
            bad.append("additional races %s" % races)

    def spec_armo(self, plug, r, sp, bad):
        if any("Non-Playable" in f for f in sp.get("record_flags", [])) and not r.flags & S.ARMO_FLAG_NON_PLAYABLE:
            bad.append("not Non-Playable")
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))
        if struct.unpack("<I", r.get("BOD2").data)[0] != int(sp["bod2_value"], 16):
            bad.append("BOD2")
        if struct.unpack("<I", r.get("RNAM").data)[0] != self.ref_fid(plug, sp["race"]):
            bad.append("race")
        arm, subs = [], r.subrecords
        for i, s in enumerate(subs):
            if s.sig == "INDX":
                nxt = subs[i + 1] if i + 1 < len(subs) else None
                arm.append((struct.unpack("<H", s.data)[0],
                            struct.unpack("<I", nxt.data)[0] if nxt is not None and nxt.sig == "MODL" else None))
        want = [(a["index"], self.ref_fid(plug, a["arma"])) for a in sp["armature"]]
        if arm != want:
            bad.append("armature %s" % arm)
        val, wt, hp = struct.unpack("<ifI", r.get("DATA").data)
        if (val, wt, hp) != (sp.get("value", 0), float(sp.get("weight", 0.0)), sp.get("health", 0)):
            bad.append("DATA %s" % ((val, wt, hp),))
        for sig in ("EITM", "PTRN", "KSIZ", "KWDA", "DESC", "OBTE"):
            if r.get(sig) is not None:
                bad.append("has %s" % sig)

    def spec_otft(self, plug, r, sp, bad):
        d = r.get("INAM").data
        items = list(struct.unpack("<%dI" % (len(d) // 4), d))
        if items != [self.ref_fid(plug, i) for i in sp["items"]]:
            bad.append("items %s" % [hx(x) for x in items])

    def spec_pack(self, plug, r, sp, bad):
        subs = r.subrecords
        pk = r.index("PKCU")
        top = [s for s in subs[:pk] if s.sig == "CTDA"]
        want = sp.get("conditions", [])
        if len(top) != len(want):
            bad.append("%d top-level conditions" % len(top))
        for s, c in zip(top, want):
            d = S.ctda_decode(s.data)
            exp = (S.CTDA_OPS[c.get("op", "==")] << 5, float(c["value"]), S.FUNC_GET_GLOBAL_VALUE,
                   self.ref_fid(plug, c["global"]), S.CTDA_RUN_ON[c.get("run_on", "Subject")], -1)
            got = (d["op_byte"], d["value"], d["function"], d["param1"], d["run_on"], d["param3"])
            if got != exp:
                bad.append("condition %s != %s" % (got, exp))
        if any(s.sig in ("CIS1", "CIS2", "VMAD") for s in subs[:pk]):
            bad.append("has CIS1/CIS2/VMAD")

    def spec_mgef(self, plug, r, sp, bad):
        d = r.get("DATA").data
        g = lambda k: struct.unpack_from("<I", d, S.MGEF_OFS[k])[0]  # noqa: E731
        if "flags" in sp:
            want = 0
            for n in sp["flags"]:
                want |= S.MGEF_FLAGS[n]
            if g("flags") != want:
                bad.append("flags %s != %s" % (hex(g("flags")), hex(want)))
            for n in ("Hostile", "Detrimental"):
                if g("flags") & S.MGEF_FLAGS[n]:
                    bad.append("%s set" % n)
        for k, table, key in (("archetype", S.MGEF_ARCHETYPE, "archetype"), ("casting_type", S.CASTING_TYPE,
                                                                              "casting_type"),
                              ("delivery", S.DELIVERY, "delivery")):
            if key in sp and g(k) != table[sp[key]]:
                bad.append("%s %d" % (k, g(k)))
        if g("assoc_item") or g("actor_value"):
            bad.append("associated item / actor value set")
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))
        if r.get("CTDA") is not None:
            bad.append("has conditions")

    def spec_spel(self, plug, r, sp, bad):
        cost, fl, typ, _ch, cast, target, _du, _rg, _pk = S.SPIT.unpack(r.get("SPIT").data)
        if "cost" in sp and cost != sp["cost"]:
            bad.append("cost %d" % cost)
        if "flags" in sp:
            want = 0
            for n in sp["flags"]:
                want |= S.SPIT_FLAGS[n]
            if fl != want:
                bad.append("flags %s != %s" % (hex(fl), hex(want)))
        if "spell_type" in sp and typ != S.SPELL_TYPE[sp["spell_type"]]:
            bad.append("type %d" % typ)
        if "casting_type" in sp and cast != S.CASTING_TYPE[sp["casting_type"]]:
            bad.append("cast type %d" % cast)
        if "delivery" in sp and target != S.DELIVERY[sp["delivery"]]:
            bad.append("target type %d" % target)
        self._effects(plug, r, sp, bad)
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))

    def _effects(self, plug, r, sp, bad):
        got, subs = [], r.subrecords
        for i, s in enumerate(subs):
            if s.sig == "EFID":
                it = subs[i + 1] if i + 1 < len(subs) and subs[i + 1].sig == "EFIT" else None
                got.append((struct.unpack("<I", s.data)[0], S.EFIT.unpack(it.data) if it is not None else None))
        want = [(self.ref_fid(plug, e["mgef"]), (float(e.get("magnitude", 0.0)), e.get("area", 0),
                                                  e.get("duration", 0))) for e in sp.get("effects", [])]
        if got != want:
            bad.append("effects %s != %s" % (got, want))
        if r.get("CTDA") is not None:
            bad.append("effect conditions present")

    def spec_alch(self, plug, r, sp, bad):
        if "weight" in sp and struct.unpack("<f", r.get("DATA").data)[0] != float(sp["weight"]):
            bad.append("weight")
        if "value" in sp and S.ENIT.unpack(r.get("ENIT").data)[0] != sp["value"]:
            bad.append("value")
        self._effects(plug, r, sp, bad)
        if sp.get("full_name") and self._str(r, "FULL") != sp["full_name"]:
            bad.append("FULL %r" % self._str(r, "FULL"))
        if r.get("DNAM") is not None:
            bad.append("has DNAM (addiction name lstring)")

    def spec_mesg(self, plug, r, sp, bad):
        if self._str(r, "FULL") != sp.get("title"):
            bad.append("title %r" % self._str(r, "FULL"))
        if self._str(r, "DESC") != sp.get("text"):
            bad.append("text %r" % self._str(r, "DESC"))
        btn = [s.zstring() for s in r.get_all("ITXT")]
        if btn != sp["buttons"]:
            bad.append("buttons %s" % btn)
        for b, i in sp.get("button_index", {}).items():
            if i >= len(btn) or btn[i] != b:
                bad.append("button %r not at index %d" % (b, i))
        fl = struct.unpack("<I", r.get("DNAM").data)[0]
        if bool(fl & S.MESG_FLAG_MESSAGE_BOX) != bool(sp.get("message_box", True)):
            bad.append("message box flag")
        # CARRY: conditions only where contract button_conditions lists them, each right after its button's ITXT and
        # exactly as specified (GetGlobalValue(<our GLOB>) op value, run on); none before the first ITXT
        want = sp.get("button_conditions", {})
        cur, got = None, {}
        for s in r.subrecords:
            if s.sig == "ITXT":
                cur = s.zstring()
                got.setdefault(cur, [])
            elif s.sig in ("CTDA", "CIS1", "CIS2"):
                if cur is None or s.sig != "CTDA":
                    bad.append("%s outside a button" % s.sig if cur is None else "%s on button %r" % (s.sig, cur))
                    continue
                d = S.ctda_decode(s.data)
                got[cur].append((d["op_byte"], d["value"], d["function"], d["param1"], d["run_on"], d["param3"]))
        for b in sp["buttons"]:
            exp = [(S.CTDA_OPS[c.get("op", "==")] << 5, float(c["value"]), S.FUNC_GET_GLOBAL_VALUE,
                    self.ref_fid(plug, c["global"]), S.CTDA_RUN_ON[c.get("run_on", "Subject")], -1)
                   for c in want.get(b, [])]
            if got.get(b, []) != exp:
                bad.append("button %r conditions %s != %s" % (b, got.get(b, []), exp))

    def spec_flst(self, plug, r, sp, bad):
        got = [struct.unpack("<I", s.data)[0] for s in r.get_all("LNAM")]
        if got != [self.ref_fid(plug, e) for e in sp["entries"]]:
            bad.append("entries %s" % [hx(x) for x in got])

    def spec_glob(self, plug, r, sp, bad):
        if struct.unpack("<f", r.get("FLTV").data)[0] != struct.unpack("<f", struct.pack("<f", sp["value"]))[0]:
            bad.append("value %r" % struct.unpack("<f", r.get("FLTV").data)[0])
        f = r.get("FNAM")
        if f is None or f.data != b"f":
            bad.append("type is not float (FNAM %r)" % (f.data if f else None))
        if r.flags & S.GLOB_FLAG_CONSTANT:
            bad.append("constant")
        cg = {g["edid"]: g["value"] for g in self.c.get("globals", [])}
        if r.edid in cg and cg[r.edid] != sp["value"]:
            bad.append("contract globals/records disagree")

    def spec_perk(self, plug, r, sp, bad):
        d = r.get("DATA").data
        if "playable" in sp and bool(d[3]) != sp["playable"]:
            bad.append("playable")
        if "hidden" in sp and bool(d[4]) != sp["hidden"]:
            bad.append("hidden")
        subs, got = r.subrecords, []
        for i, s in enumerate(subs):
            if s.sig == "PRKE" and s.data[0] == 2:
                dd = subs[i + 1].data
                epfd = next((x for x in subs[i + 1:] if x.sig in ("EPFD", "PRKF")), None)
                got.append((dd[0], dd[1], dd[2], struct.unpack("<f", epfd.data)[0]
                            if epfd is not None and epfd.sig == "EPFD" else None))
        want = []
        for text in sp.get("entry_points", []):
            m = re.match(r"\s*([^:]+):\s*(Set Value|Add Value|Multiply Value)\s+(-?[0-9.]+)", text)
            ep = PERK_ENTRY_POINTS[m.group(1).strip()]
            # EPFD is an f32: compare with the wanted value rounded through f32 (0.01 reads back 0.0099999998)
            want.append((ep, S.PERK_FUNCTIONS[m.group(2)], PERK_ENTRY_TABS[ep],
                         struct.unpack("<f", struct.pack("<f", float(m.group(3))))[0]))
        if got != want:
            bad.append("entry points %s != %s" % (got, want))

    def spec_expl(self, plug, r, sp, bad):
        d = r.get("DATA").data
        O = S.EXPL_OFS
        f = lambda k: struct.unpack_from("<f", d, O[k])[0]  # noqa: E731
        if "force" in sp and f("force") != sp["force"]:
            bad.append("force %r" % f("force"))
        if "damage" in sp and f("damage") != sp["damage"]:
            bad.append("damage %r" % f("damage"))
        if "radius" in sp and (f("inner_radius"), f("outer_radius")) != (sp["radius"], sp["radius"]):
            bad.append("radius")
        fl = struct.unpack_from("<I", d, O["flags"])[0]
        if sp.get("knock_down") == "Never" and fl & (S.EXPL_FLAG_KNOCKDOWN_ALWAYS | S.EXPL_FLAG_KNOCKDOWN_FORMULA):
            bad.append("knock down flags %s" % hex(fl))
        if "stagger" in sp and struct.unpack_from("<I", d, O["stagger"])[0] != S.EXPL_STAGGER[sp["stagger"]]:
            bad.append("stagger")
        if r.get("MNAM") is not None:
            bad.append("image space modifier MNAM present (contract: sound/particles only)")

    def spec_sndr(self, plug, r, sp, bad):
        order = [s.sig for s in r.subrecords]
        # vanilla order EDID CNAM GNAM ANAM* ONAM LNAM BNAM (fo4_schema.SNDR_ORDER), nothing else
        nanam = order.count("ANAM")
        want_order = ["EDID", "CNAM", "GNAM"] + ["ANAM"] * nanam + ["ONAM", "LNAM", "BNAM"]
        if order != want_order:
            bad.append("subrecords %s, want %s" % (" ".join(order), " ".join(want_order)))
        cn = r.get("CNAM")
        if cn is None or cn.data != struct.pack("<I", S.SNDR_CNAM[sp.get("descriptor_type", "Standard")]):
            bad.append("CNAM %s is not %s" % (cn.data.hex() if cn else None, sp.get("descriptor_type", "Standard")))
        for sig, key in (("GNAM", "category"), ("ONAM", "output_model")):
            s = r.get(sig)
            want = self.ref_fid(plug, sp[key])
            if s is None or len(s.data) != 4 or struct.unpack("<I", s.data)[0] != want:
                bad.append("%s %s, want %s %s" % (sig, s.data.hex() if s else None, sp[key], hx(want)))
        got = [s.data for s in r.get_all("ANAM")]
        want = [ew.zstr(f["anam"]) for f in sp.get("files", [])]
        if got != want:
            bad.append("ANAM list %s != contract %s" % ([g.rstrip(b"\0").decode("latin-1") for g in got][:3],
                                                        [f["anam"] for f in sp.get("files", [])][:3]))
        ln = r.get("LNAM")
        want_ln = S.SNDR_LNAM.pack(0, S.SNDR_LOOPING[sp.get("looping", "None")], 0, 0)
        if ln is None or ln.data != want_ln:
            bad.append("LNAM %s, want %s (looping %s)" % (ln.data.hex() if ln else None, want_ln.hex(),
                                                          sp.get("looping", "None")))
        bn = r.get("BNAM")
        if bn is None or len(bn.data) != S.SNDR_BNAM.size:
            bad.append("BNAM missing or not %d bytes" % S.SNDR_BNAM.size)
        else:
            shift, var, pri, dbv, att = S.SNDR_BNAM.unpack(bn.data)
            want_b = (sp.get("frequency_shift_pct", 0), sp.get("frequency_variance_pct", 0), sp.get("priority", 128),
                      sp.get("db_variance", 0), int(round(sp.get("static_attenuation_db", 0.0) * 100)))
            if (shift, var, pri, dbv, att) != want_b:
                bad.append("BNAM %s != contract %s" % ((shift, var, pri, dbv, att), want_b))

    # ---------- sound files
    def check_sounds(self, plug, tag):
        """Every SNDR ANAM names a file staged in build/Data: 16-bit PCM WAV with the contract's channels/rate/frames,
        byte-identical (sha1) to the extracted clip, listed in contract paths.files_main; nothing else is staged."""
        import hashlib
        sndrs = [r for r in plug.records if r.sig == "SNDR"]
        if not sndrs:
            return
        sys.path.insert(0, os.path.join(os.path.dirname(HERE), "sounds"))
        import wavinfo
        spec_files = {}
        for w in self.c["records"]:
            if w["type"] == "SNDR":
                for f in w["spec"].get("files", []):
                    spec_files[f["anam"].lower()] = f
        listed = {f.replace("\\", "/").lower() for f in self.c["paths"].get("files_main", [])}
        bad, n, used = [], 0, set()
        for r in sndrs:
            for s in r.get_all("ANAM"):
                anam = s.zstring()
                n += 1
                rel = anam[5:] if anam.lower().startswith("data\\") else anam
                used.add(rel.replace("\\", "/").lower())
                f = spec_files.get(anam.lower())
                if f is None:
                    bad.append("%s ANAM %s is not a contract file" % (r.edid, anam))
                    continue
                if ("build/data/" + f["data_path"]).lower() not in listed:
                    bad.append("%s %s not in paths.files_main" % (r.edid, f["data_path"]))
                full = os.path.join(self.a.build_data, rel)
                if not os.path.isfile(full):
                    bad.append("%s %s not staged in build/Data (run tools/sounds/stage_sounds.py)" % (r.edid, rel))
                    continue
                data = open(full, "rb").read()
                probs = wavinfo.pcm16_problems(wavinfo.wav_info(data), f["channels"], f["rate"], f["frames"])
                if probs:
                    bad.append("%s %s: %s" % (r.edid, rel, "; ".join(probs)))
                if hashlib.sha1(data).hexdigest() != f["sha1"]:
                    bad.append("%s %s differs from its source %s (sha1)" % (r.edid, rel, f["source"]))
        self.R.check(not bad, tag + "every SNDR sound file is staged, 16-bit PCM and = its source (%d)" % n,
                     "; ".join(bad[:6]))
        root = os.path.join(self.a.build_data, "Sound")
        extra = []
        for dp, _dn, fn in os.walk(root):
            for x in fn:
                rel = os.path.relpath(os.path.join(dp, x), self.a.build_data).replace("\\", "/").lower()
                if rel not in used:
                    extra.append(rel)
        self.R.check(not extra, tag + "no unreferenced file under build/Data/Sound", ", ".join(sorted(extra)[:6]))

    # ---------- paths
    def check_paths(self, plug, tag):
        vanilla = load_vanilla_paths()
        bad, n = [], 0
        for r in plug.records:
            for s in r.subrecords:
                if s.sig not in S.MODEL_PATH_SIGS:
                    continue
                if s.sig == "MODL" and r.sig in S.MODEL_PATH_FORMID_TYPES:
                    continue
                rel = s.zstring()
                n += 1
                local = os.path.join(self.a.build_data, "Meshes", rel)
                if os.path.isfile(local):
                    continue
                key = ("meshes\\" + rel).lower().replace("/", "\\")
                if vanilla is None:
                    bad.append("%s %s %s not in build/Data (no vanilla index to check)" % (r.edid, s.sig, rel))
                elif key not in vanilla:
                    bad.append("%s %s %s exists nowhere" % (r.edid, s.sig, rel))
        self.R.check(not bad, tag + "every model path exists (build/Data or vanilla archives) (%d)" % n,
                     "; ".join(bad))

    # ---------- scripts
    def check_scripts(self):
        R = self.R
        plugs = {os.path.basename(p.path): p for p in (self.main, self.test) if p is not None}
        for name, s in self.c["scripts"].items():
            at = s.get("attached_to")
            if not at:
                continue
            rec_spec = self.records_by_edid.get(at["record"])
            plug = plugs.get(rec_spec["plugin"]) if rec_spec else None
            if plug is None:
                continue
            rec = plug.by_edid.get(at["record"])
            if rec is None:
                continue
            props = vmad_props_for(rec, name)
            psc_path = os.path.join(FO4W, s["file"].replace("/", os.sep))
            if self.a.psc_root:
                psc_path = os.path.join(self.a.psc_root, *s["file"].split("/")[2:])
            tag = "%s (%s): " % (name, os.path.basename(psc_path))
            if not os.path.isfile(psc_path):
                if self.a.partial:
                    R.note(tag + ".psc not written yet", psc_path)
                else:
                    R.check(False, tag + ".psc exists", psc_path)
                continue
            with open(psc_path, encoding="utf-8", errors="replace") as f:
                ps = parse_psc(f.read())
            R.check(not ps["errors"], tag + "parses", "; ".join(ps["errors"]))
            R.check((ps["name"] or "").lower() == name.lower() and
                    (ps["extends"] or "").lower() == (s.get("extends") or "").lower(),
                    tag + "Scriptname %s extends %s" % (name, s.get("extends")), "%s extends %s" %
                    (ps["name"], ps["extends"]))
            self.check_psc_props(plug, rec, name, s, ps, props, tag)
            self.check_pex(name, s, ps, tag)

    def check_psc_props(self, plug, rec, name, s, ps, props, tag):
        R = self.R
        decl = ps["properties"]
        filled = {k.lower(): v for k, v in props.items()}
        bad = []
        for prop in s.get("properties", []):
            k = prop["name"].lower()
            d = decl.get(k)
            if d is None:
                bad.append("%s not declared" % prop["name"])
                continue
            if d["type"].lower() != prop["type"].lower():
                bad.append("%s type %s, contract %s" % (prop["name"], d["type"], prop["type"]))
            want_flags = contract_flags(prop["flags"])
            if d["flags"] != want_flags:
                bad.append("%s flags '%s', contract '%s'" % (prop["name"], " ".join(sorted(d["flags"])),
                                                             prop["flags"]))
        R.check(not bad, tag + "declares every contract property with its type and flags (%d)" %
                len(s.get("properties", [])), "; ".join(bad))
        # every declared fillable property must be in the contract (rule 3); mandatory ones must be filled
        contract_names = {p["name"].lower() for p in s.get("properties", [])}
        hidden_names = {p["name"].lower() for p in s.get("hidden_properties", [])}
        bad, ro = [], []
        for k, d in decl.items():
            if "hidden" in d["flags"]:
                continue
            if d.get("readonly"):
                ro.append(d["name"])
                continue
            if k not in contract_names:
                bad.append("%s (%s) is a fillable non-hidden property not in the contract" % (d["name"], d["type"]))
        R.check(not bad, tag + "declares no other fillable non-hidden property", "; ".join(bad))
        if ro:
            R.note(tag + "non-hidden AutoReadOnly constants (not fillable, ignored)", ", ".join(ro))
        bad = [d["name"] for k, d in decl.items() if "mandatory" in d["flags"] and
               (k not in filled or filled[k]["value"] is None or
                (filled[k]["type"] == vm.T_OBJECT and filled[k]["value"]["formid"] == 0))]
        R.check(not bad, tag + "every Mandatory property is filled", ", ".join(bad))
        # filled forms fit the declared type
        bad = []
        for k, p in filled.items():
            d = decl.get(k)
            if d is None:
                bad.append("VMAD fills %s, which the .psc does not declare" % p["name"])
                continue
            msg = self.type_fits(plug, d["type"], p)
            if msg:
                bad.append("%s: %s" % (p["name"], msg))
        R.check(not bad, tag + "every filled value fits the declared type (%d)" % len(filled), "; ".join(bad))
        missing = [h["name"] for h in s.get("hidden_properties", []) if h["name"].lower() not in decl]
        wrong = [h["name"] for h in s.get("hidden_properties", []) if h["name"].lower() in decl and
                 "hidden" not in decl[h["name"].lower()]["flags"]]
        if missing or wrong:
            R.warn(tag + "contract runtime properties (C's interface)", "missing %s; not Hidden %s" % (missing, wrong))

    def type_fits(self, plug, ptype, p):
        t = ptype.lower()
        if t.endswith("[]"):
            return None if p["type"] > vm.T_ARRAY_BASE else "declared array, VMAD type %d" % p["type"]
        if t in S.PAPYRUS_PRIMITIVES:
            want = {"int": vm.T_INT, "float": vm.T_FLOAT, "bool": vm.T_BOOL, "string": vm.T_STRING, "var": vm.T_VAR}
            return None if p["type"] == want[t] else "VMAD type %d" % p["type"]
        if p["type"] != vm.T_OBJECT:
            return "VMAD type %d for an object type" % p["type"]
        fid, alias = p["value"]["formid"], p["value"]["alias"]
        if t in S.PAPYRUS_ALIAS_TYPES:
            if alias < 0:
                return "alias type without an alias id"
            q = self.record_of(plug, fid)
            if q is None or q.sig != "QUST":
                return "alias owner %s is not one of our quests" % hx(fid)
            if alias_ids(q).get(alias) != S.PAPYRUS_ALIAS_TYPES[t]:
                return "alias %d of %s is not a %s" % (alias, q.edid, ptype)
            return None
        if alias >= 0:
            return "alias value for non-alias type %s" % ptype
        sig, src = self.file_record_type(plug, fid)
        if sig is None:
            return "%s does not resolve (%s)" % (hx(fid), src)
        cs = {k.lower(): v for k, v in self.c["scripts"].items()}
        if t in cs:
            at = cs[t].get("attached_to") or {}
            r = self.record_of(plug, fid)
            if r is None or r.edid != at.get("record"):
                return "%s must hold %s (the record carrying %s)" % (ptype, at.get("record"), ptype)
            if ptype.lower() not in [n.lower() for n in vmad_script_names(r)]:
                return "%s does not carry script %s" % (r.edid, ptype)
            return None
        allowed = S.PAPYRUS_FORM_TYPES.get(t, "unknown")
        if allowed == "unknown":
            self.R.warn("type %s not in fo4_schema.PAPYRUS_FORM_TYPES; %s not type-checked" % (ptype, p["name"]))
            return None
        if allowed is not None and sig not in allowed:
            return "%s is a %s, %s needs %s" % (hx(fid), sig, ptype, sorted(allowed))
        return None

    def check_pex(self, name, s, ps, tag):
        parts = name.split(":")
        roots = [self.a.pex_root, os.path.join(FO4W, "build", "test", "Data", "Scripts")]
        pex = None
        for root in roots:
            cand = os.path.join(root, *parts) + ".pex"
            if os.path.isfile(cand):
                pex = cand
                break
        if pex is None:
            self.R.note(tag + "no compiled .pex yet (pex check skipped)")
            return
        try:
            with open(pex, "rb") as f:
                px = parse_pex(f.read())
        except (ValueError, struct.error, IndexError) as e:
            self.R.check(False, tag + ".pex readable", "%s: %s" % (pex, e))
            return
        bad = []
        if px["script"].lower() != name.lower():
            bad.append("script name %s" % px["script"])
        mand = px["user_flags"].get("mandatory")
        hid = px["user_flags"].get("hidden")
        if any("mandatory" in d["flags"] for d in ps["properties"].values()) and mand is None:
            bad.append(".pex has no 'mandatory' user flag although the .psc uses Mandatory")
        for k, d in ps["properties"].items():
            q = px["properties"].get(k)
            if q is None:
                bad.append("%s missing in .pex" % d["name"])
                continue
            if q["type"].lower() != d["type"].lower():
                bad.append("%s type %s in .pex" % (d["name"], q["type"]))
            if bool(q["auto"]) != bool(d["auto"]):
                bad.append("%s is %s in .pex" % (d["name"], "Auto" if q["auto"] else "not Auto"))
            if d.get("readonly") and not q.get("readonly"):
                bad.append("%s AutoReadOnly in .psc, not read-only in .pex" % d["name"])
            if mand is not None and bool(q["user_flags"] & (1 << mand)) != ("mandatory" in d["flags"]):
                bad.append("%s Mandatory differs" % d["name"])
            if hid is not None and bool(q["user_flags"] & (1 << hid)) != ("hidden" in d["flags"]):
                bad.append("%s Hidden differs" % d["name"])
            if q.get("const") is not None and q["auto"] and bool(q["const"]) != ("const" in d["flags"]):
                bad.append("%s Const differs" % d["name"])
        extra = [q["name"] for k, q in px["properties"].items() if k not in ps["properties"]]
        if extra:
            bad.append("in .pex but not in .psc: %s" % extra)
        mt_psc = os.path.getmtime(os.path.join(FO4W, s["file"].replace("/", os.sep))) \
            if os.path.isfile(os.path.join(FO4W, s["file"].replace("/", os.sep))) else 0
        if os.path.getmtime(pex) < mt_psc:
            self.R.warn(tag + ".pex is older than the .psc (rebuild?)", pex)
        self.R.check(not bad, tag + ".pex property table = .psc", "; ".join(bad))


# ------------------------------------------------------------------ helpers
def vmad_objects(d):
    """Every Object value in a decoded VMAD (record scripts, alias entries, alias scripts, arrays, structs)."""
    out = []

    def props(ps):
        for p in ps:
            walk(p["type"], p["value"])

    def walk(t, v):
        if t == vm.T_OBJECT:
            out.append(v)
        elif t == vm.T_VAR and isinstance(v, dict):
            walk(v["type"], v["value"])
        elif t == vm.T_STRUCT:
            props(v)
        elif t > vm.T_ARRAY_BASE:
            for e in v:
                walk(t - vm.T_ARRAY_BASE, e)

    for s in d["scripts"]:
        props(s["properties"])
    fr = d.get("fragments")
    if fr and "aliases" in fr:
        for a in fr["aliases"]:
            out.append(a["object"])
            for s in a["scripts"]:
                props(s["properties"])
    return out


def vmad_script_names(rec):
    v = rec.get("VMAD")
    if v is None:
        return []
    d = vm.decode(v.data, rec.sig)
    names = [s["name"] for s in d["scripts"]]
    fr = d.get("fragments")
    if fr and "aliases" in fr:
        names += [s["name"] for a in fr["aliases"] for s in a["scripts"]]
    return names


def vmad_props_for(rec, script_name):
    """{name: property} of script_name in the record's VMAD (record scripts or alias scripts)."""
    v = rec.get("VMAD")
    if v is None:
        return {}
    d = vm.decode(v.data, rec.sig)
    scripts = list(d["scripts"])
    fr = d.get("fragments")
    if fr and "aliases" in fr:
        scripts += [s for a in fr["aliases"] for s in a["scripts"]]
    for s in scripts:
        if s["name"].lower() == script_name.lower():
            return {p["name"]: p for p in s["properties"]}
    return {}


def parse_aliases(rec):
    out, cur = [], None
    for s in rec.subrecords:
        if s.sig in ("ALST", "ALLS", "ALCS"):
            cur = {"kind": s.sig, "id": struct.unpack("<I", s.data)[0], "seq": [s.sig]}
            out.append(cur)
            continue
        if cur is None:
            continue
        cur["seq"].append(s.sig)
        if s.sig == "ALID":
            cur["ALID"] = s.zstring()
        elif s.sig == "FNAM":
            cur["FNAM"] = struct.unpack("<I", s.data)[0]
        elif s.sig == "ALPC":
            cur.setdefault("ALPC", []).append(struct.unpack("<I", s.data)[0])
        elif s.sig in S.ALIAS_FILL_SIGS:
            cur[s.sig] = struct.unpack("<I", s.data)[0] if len(s.data) == 4 else s.data
        elif s.sig == "ALED":
            cur = None
    return out


def alias_ids(rec):
    return {a["id"]: a["kind"] for a in parse_aliases(rec)}


_VANILLA_PATHS = []


def load_vanilla_paths():
    if not _VANILLA_PATHS:
        s = None
        try:
            with gzip.open(VANILLA_PATHS, "rt", encoding="utf-8") as f:
                s = set(l.split("\t", 1)[0] for l in f if not l.startswith("#"))
        except OSError:
            s = None
        _VANILLA_PATHS.append(s)
    return _VANILLA_PATHS[0]


def run_xedit(R, main_path, test_path):
    ps1 = os.path.join(FO4W, "helpers", "xedit-headless", "run_check.ps1")
    if not os.path.isfile(ps1):
        R.note("xedit: helpers/xedit-headless/run_check.ps1 not present (L1 skipped)")
        return
    plugins = [os.path.basename(main_path)] + ([os.path.basename(test_path)] if test_path else [])
    dirs = [os.path.dirname(main_path)] + ([os.path.dirname(test_path)] if test_path else [])
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1, "-Plugin", ",".join(plugins),
           "-DataDir", ";".join(dirs), "-Strict"]
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=1800)
    except (OSError, subprocess.TimeoutExpired) as e:
        R.check(False, "xedit Check for Errors (helpers/xedit-headless, -Strict)", str(e))
        return
    m = re.search(r"^RESULT errors=(-?\d+)(.*)$", r.stdout, re.M)
    errs = [l for l in r.stdout.splitlines() if l.startswith(("ERR ", "XTRA", "XWARN"))]
    n = int(m.group(1)) if m else -1
    R.check(n == 0, "xedit Check for Errors on %s: errors=%d (helpers/xedit-headless, -Strict)" %
            (",".join(plugins), n), "; ".join(errs[:5]) or (m.group(2).strip() if m else r.stdout[-300:]))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--main", default=os.path.join(FO4W, "build", "Data", "SuperPeasant.esp"))
    ap.add_argument("--test", default=os.path.join(FO4W, "build", "test", "Data", "SuperPeasant_Test.esp"))
    ap.add_argument("--no-test", action="store_true", help="validate SuperPeasant.esp only")
    ap.add_argument("--contract", default=os.path.join(FO4W, "native", "papyrus_scripts", "contract.json"))
    ap.add_argument("--esm", default=DEFAULT_ESM)
    ap.add_argument("--psc-root", default=None, help="folder that holds SuperPeasant\\*.psc (default: contract paths)")
    ap.add_argument("--build-data", default=os.path.join(FO4W, "build", "Data"))
    ap.add_argument("--pex-root", default=os.path.join(FO4W, "build", "Data", "Scripts"))
    ap.add_argument("--partial", action="store_true", help="missing .psc files are NOTEs (scripts still being written)")
    ap.add_argument("--xedit", action="store_true", help="also run xEdit Check for Errors (helpers/xedit-headless)")
    ap.add_argument("--json", default=None)
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)
    R = Results(a.quiet)
    for p in (a.contract, a.esm, a.main) + (() if a.no_test else (a.test,)):
        if not os.path.isfile(p):
            print("ERROR missing input: %s" % p)
            print("RESULT pass=0 fail=1")
            return 2
    with open(a.contract, encoding="utf-8") as f:
        contract = json.load(f)
    V = Validator(a, contract, R)
    try:
        V.main = Loaded(a.main, contract["plugin"])
        V.test = None if a.no_test else Loaded(a.test, contract["test_plugin"])
    except (er.ESPFormatError, struct.error, OSError) as e:
        R.check(False, "plugins parse", str(e))
        print("RESULT pass=%d fail=%d" % (R.npass, R.nfail))
        return 1
    R.check(True, "plugins parse (%s)" % ", ".join(os.path.basename(p.path) for p in (V.main, V.test) if p))
    wanted = set()
    for p in (V.main, V.test):
        if p is not None:
            wanted |= V.collect_fids(p)
    for v in contract["vanilla"].values():
        wanted.add(int(v["formid"], 16) & 0xFFFFFF)
    V.esm_types = scan_types(a.esm, wanted)
    for p, meta, is_test in ((V.main, contract["plugin"], False), (V.test, contract["test_plugin"], True)):
        if p is not None:
            V.check_plugin(p, meta, is_test)
    # every NIF path the contract names exists in build/Data
    nifs = contract["paths"].get("nif_paths_in_esp", {})
    miss = [v for v in nifs.values() if not os.path.isfile(os.path.join(a.build_data, "Meshes", v))]
    R.check(not miss, "contract NIF paths exist in build/Data/Meshes (%d)" % len(nifs), ", ".join(miss))
    V.check_scripts()
    if a.xedit:
        run_xedit(R, a.main, None if a.no_test else a.test)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump({"pass": R.npass, "fail": R.nfail, "rows": R.rows}, f, indent=1)
    print("RESULT pass=%d fail=%d" % (R.npass, R.nfail))
    return 0 if R.nfail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
