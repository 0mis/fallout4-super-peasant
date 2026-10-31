"""Fallout 4 VMAD (script attachment) codec: decode() <-> encode(), byte-exact, stdlib only.

Layout (version 6, object format 2, as in every Fallout4.esm VMAD except one QUST at version 5):

  VMAD     = version:i16 objFormat:i16 scriptCount:u16 Script[scriptCount] [Fragments, record-type specific]
  Script   = name:wstr flags:u8 propertyCount:u16 Property[propertyCount]
               flags: 0 local, 1 inherited, 2 removed, 3 inherited+removed
  Property = name:wstr type:u8 flags:u8 value
               flags: 1 edited, 3 removed
  value by type: 1 Object  (format 2) unused:u16 alias:i16 formid:u32   (format 1) formid:u32 alias:i16 unused:u16
                 2 String wstr   3 Int i32   4 Float f32   5 Bool u8
                 6 Variable = type:u8 + value of that type (only seen nested? decoded generically)
                 7 Struct = memberCount:u32 Property[memberCount]
                 11..17 Array of (type - 10) = count:u32 values  (17 = array of structs: each is memberCount + members)
  wstr = u16 length + bytes (no terminator)

  Fragments, by record type:
    QUST: extraBindVersion:i8 fragmentCount:u16 Script
          QuestFragment[fragmentCount] = stage:u16 unknown:i16 stageIndex:i32 unknown:i8 scriptName:wstr fragName:wstr
          aliasCount:u16 Alias[aliasCount] = Object(8 bytes, as type 1) version:i16 objFormat:i16 scriptCount:u16 Script[]
    PACK: extraBindVersion:i8 flags:u8 (1 OnBegin, 2 OnEnd, 4 OnChange) Script
          PackFragment[popcount(flags)] = unknown:i8 scriptName:wstr fragName:wstr
    PERK: extraBindVersion:i8 Script fragmentCount:u16
          PerkFragment[n] = index:u16 unknown:i16 unknown:i8 scriptName:wstr fragName:wstr
  The fragment block is absent when the VMAD ends after the scripts (e.g. a quest with one script and no aliases).
  Other record types: any trailing bytes are kept verbatim in "tail" (none exist in our 14 types).

Decoded form (plain dicts/lists, JSON friendly):
  {"version": 6, "obj_format": 2, "scripts": [Script], "fragments": None | {...}, "tail": b""}
  Script   {"name": str, "flags": int, "properties": [Property]}
  Property {"name": str, "type": int, "flags": int, "value": ...}
     Object value: {"formid": int, "alias": int, "unused": int}; Struct value: [Property]; arrays: lists.

Builders for agent B2 (build_esp.py): prop_object / prop_int / prop_float / prop_bool / prop_string / prop_array /
script(name, props) / vmad_bytes(scripts, rec_sig, aliases=None). Strings are UTF-8 with surrogateescape, so any
vanilla byte sequence survives a round trip.
"""
import struct

T_NONE, T_OBJECT, T_STRING, T_INT, T_FLOAT, T_BOOL, T_VAR, T_STRUCT = 0, 1, 2, 3, 4, 5, 6, 7
T_ARRAY_BASE = 10
TYPE_NAMES = {0: "None", 1: "Object", 2: "String", 3: "Int", 4: "Float", 5: "Bool", 6: "Var", 7: "Struct",
              11: "Object[]", 12: "String[]", 13: "Int[]", 14: "Float[]", 15: "Bool[]", 16: "Var[]", 17: "Struct[]"}
PAPYRUS_TYPE = {"int": T_INT, "float": T_FLOAT, "bool": T_BOOL, "string": T_STRING, "var": T_VAR}

FLAG_EDITED = 1
FLAG_REMOVED = 3
ALIAS_NONE = -1


class VMADError(ValueError):
    pass


class _R(object):
    __slots__ = ("d", "i")

    def __init__(self, d):
        self.d = d
        self.i = 0

    def take(self, n):
        if self.i + n > len(self.d):
            raise VMADError("VMAD overrun at %d (+%d of %d)" % (self.i, n, len(self.d)))
        b = self.d[self.i:self.i + n]
        self.i += n
        return b

    def u(self, fmt):
        s = struct.calcsize("<" + fmt)
        v = struct.unpack("<" + fmt, self.take(s))
        return v[0] if len(v) == 1 else v

    def wstr(self):
        n = self.u("H")
        return self.take(n).decode("utf-8", errors="surrogateescape")

    def left(self):
        return len(self.d) - self.i


def _w_wstr(out, s):
    b = s.encode("utf-8", errors="surrogateescape")
    if len(b) > 0xFFFF:
        raise VMADError("string too long")
    out.append(struct.pack("<H", len(b)))
    out.append(b)


# ---------------------------------------------------------------- values
def _r_object(r, fmt):
    if fmt == 2:
        unused, alias, formid = r.u("HhI")
    else:
        formid, alias, unused = r.u("IhH")
    return {"formid": formid, "alias": alias, "unused": unused}


def _w_object(out, v, fmt):
    if fmt == 2:
        out.append(struct.pack("<HhI", v.get("unused", 0), v.get("alias", ALIAS_NONE), v["formid"] & 0xFFFFFFFF))
    else:
        out.append(struct.pack("<IhH", v["formid"] & 0xFFFFFFFF, v.get("alias", ALIAS_NONE), v.get("unused", 0)))


def _r_value(r, t, fmt):
    if t == T_NONE:
        return None
    if t == T_OBJECT:
        return _r_object(r, fmt)
    if t == T_STRING:
        return r.wstr()
    if t == T_INT:
        return r.u("i")
    if t == T_FLOAT:
        return r.u("f")
    if t == T_BOOL:
        return r.u("B")
    if t == T_VAR:
        vt = r.u("B")
        return {"type": vt, "value": _r_value(r, vt, fmt)}
    if t == T_STRUCT:
        n = r.u("I")
        return [_r_property(r, fmt) for _ in range(n)]
    if T_ARRAY_BASE < t <= T_ARRAY_BASE + T_STRUCT:
        n = r.u("I")
        return [_r_value(r, t - T_ARRAY_BASE, fmt) for _ in range(n)]
    raise VMADError("unknown property type %d at %d" % (t, r.i))


def _w_value(out, t, v, fmt):
    if t == T_NONE:
        return
    if t == T_OBJECT:
        _w_object(out, v, fmt)
    elif t == T_STRING:
        _w_wstr(out, v)
    elif t == T_INT:
        out.append(struct.pack("<i", v))
    elif t == T_FLOAT:
        out.append(struct.pack("<f", v))
    elif t == T_BOOL:
        out.append(struct.pack("<B", int(v)))
    elif t == T_VAR:
        out.append(struct.pack("<B", v["type"]))
        _w_value(out, v["type"], v["value"], fmt)
    elif t == T_STRUCT:
        out.append(struct.pack("<I", len(v)))
        for m in v:
            _w_property(out, m, fmt)
    elif T_ARRAY_BASE < t <= T_ARRAY_BASE + T_STRUCT:
        out.append(struct.pack("<I", len(v)))
        for e in v:
            _w_value(out, t - T_ARRAY_BASE, e, fmt)
    else:
        raise VMADError("unknown property type %d" % t)


def _r_property(r, fmt):
    name = r.wstr()
    t, fl = r.u("BB")
    return {"name": name, "type": t, "flags": fl, "value": _r_value(r, t, fmt)}


def _w_property(out, p, fmt):
    _w_wstr(out, p["name"])
    out.append(struct.pack("<BB", p["type"], p.get("flags", FLAG_EDITED)))
    _w_value(out, p["type"], p["value"], fmt)


def _r_script(r, fmt):
    name = r.wstr()
    if name == "":
        # an empty script name (the fragment-block script of a quest with alias scripts but no stage
        # fragments) is written as just the zero length: no flags, no property count
        return {"name": "", "flags": 0, "properties": []}
    fl = r.u("B")
    n = r.u("H")
    return {"name": name, "flags": fl, "properties": [_r_property(r, fmt) for _ in range(n)]}


def _w_script(out, s, fmt):
    _w_wstr(out, s["name"])
    if s["name"] == "":
        if s.get("properties"):
            raise VMADError("a script with an empty name cannot carry properties")
        return
    out.append(struct.pack("<BH", s.get("flags", 0), len(s["properties"])))
    for p in s["properties"]:
        _w_property(out, p, fmt)


# ---------------------------------------------------------------- fragments
def _r_frag_qust(r, fmt):
    f = {"extra_version": r.u("b")}
    n = r.u("H")
    f["script"] = _r_script(r, fmt)
    frags = []
    for _ in range(n):
        stage, u1, idx, u2 = r.u("HhiB")
        frags.append({"stage": stage, "unknown1": u1, "stage_index": idx, "unknown2": u2,
                      "script_name": r.wstr(), "fragment_name": r.wstr()})
    f["fragments"] = frags
    na = r.u("H")
    aliases = []
    for _ in range(na):
        obj = _r_object(r, fmt)
        ver, afmt, ns = r.u("hhH")
        aliases.append({"object": obj, "version": ver, "obj_format": afmt,
                        "scripts": [_r_script(r, afmt) for _ in range(ns)]})
    f["aliases"] = aliases
    return f


def _w_frag_qust(out, f, fmt):
    out.append(struct.pack("<bH", f.get("extra_version", 3), len(f["fragments"])))
    _w_script(out, f["script"], fmt)
    for x in f["fragments"]:
        out.append(struct.pack("<HhiB", x["stage"], x.get("unknown1", 0), x["stage_index"], x.get("unknown2", 1)))
        _w_wstr(out, x["script_name"])
        _w_wstr(out, x["fragment_name"])
    out.append(struct.pack("<H", len(f["aliases"])))
    for a in f["aliases"]:
        _w_object(out, a["object"], fmt)
        afmt = a.get("obj_format", 2)
        out.append(struct.pack("<hhH", a.get("version", 6), afmt, len(a["scripts"])))
        for s in a["scripts"]:
            _w_script(out, s, afmt)


def _r_frag_pack(r, fmt):
    f = {"extra_version": r.u("b"), "flags": r.u("B")}
    f["script"] = _r_script(r, fmt)
    frags = []
    for _ in range(bin(f["flags"]).count("1")):
        u = r.u("b")
        frags.append({"unknown": u, "script_name": r.wstr(), "fragment_name": r.wstr()})
    f["fragments"] = frags
    return f


def _w_frag_pack(out, f, fmt):
    if bin(f["flags"]).count("1") != len(f["fragments"]):
        raise VMADError("PACK fragment flags do not match the fragment count")
    out.append(struct.pack("<bB", f.get("extra_version", 3), f["flags"]))
    _w_script(out, f["script"], fmt)
    for x in f["fragments"]:
        out.append(struct.pack("<b", x.get("unknown", 1)))
        _w_wstr(out, x["script_name"])
        _w_wstr(out, x["fragment_name"])


def _r_frag_perk(r, fmt):
    f = {"extra_version": r.u("b")}
    f["script"] = _r_script(r, fmt)
    n = r.u("H")
    frags = []
    for _ in range(n):
        idx, u1, u2 = r.u("HhB")
        frags.append({"index": idx, "unknown1": u1, "unknown2": u2, "script_name": r.wstr(),
                      "fragment_name": r.wstr()})
    f["fragments"] = frags
    return f


def _w_frag_perk(out, f, fmt):
    out.append(struct.pack("<b", f.get("extra_version", 3)))
    _w_script(out, f["script"], fmt)
    out.append(struct.pack("<H", len(f["fragments"])))
    for x in f["fragments"]:
        out.append(struct.pack("<HhB", x["index"], x.get("unknown1", 0), x.get("unknown2", 1)))
        _w_wstr(out, x["script_name"])
        _w_wstr(out, x["fragment_name"])


_FRAG = {"QUST": (_r_frag_qust, _w_frag_qust), "PACK": (_r_frag_pack, _w_frag_pack),
         "PERK": (_r_frag_perk, _w_frag_perk)}


# ---------------------------------------------------------------- public
def decode(data, rec_sig):
    r = _R(bytes(data))
    ver, fmt, n = r.u("hhH")
    if fmt not in (1, 2):
        raise VMADError("unknown object format %d" % fmt)
    v = {"version": ver, "obj_format": fmt, "scripts": [_r_script(r, fmt) for _ in range(n)], "fragments": None,
         "tail": b""}
    if r.left() and rec_sig in _FRAG:
        v["fragments"] = _FRAG[rec_sig][0](r, fmt)
    if r.left():
        if rec_sig in _FRAG:
            raise VMADError("%d bytes left after %s fragments" % (r.left(), rec_sig))
        v["tail"] = r.take(r.left())
    return v


def encode(v, rec_sig):
    out = []
    fmt = v.get("obj_format", 2)
    out.append(struct.pack("<hhH", v.get("version", 6), fmt, len(v["scripts"])))
    for s in v["scripts"]:
        _w_script(out, s, fmt)
    if v.get("fragments") is not None:
        if rec_sig not in _FRAG:
            raise VMADError("no fragment layout for %s" % rec_sig)
        _FRAG[rec_sig][1](out, v["fragments"], fmt)
    out.append(v.get("tail", b"") or b"")
    return b"".join(out)


# ---------------------------------------------------------------- builders
def prop_object(name, formid, alias=ALIAS_NONE):
    """An Object property. For a quest alias property use the quest's FormID and the alias ID."""
    return {"name": name, "type": T_OBJECT, "flags": FLAG_EDITED, "value": {"formid": formid, "alias": alias,
                                                                            "unused": 0}}


def prop_int(name, v):
    return {"name": name, "type": T_INT, "flags": FLAG_EDITED, "value": int(v)}


def prop_float(name, v):
    return {"name": name, "type": T_FLOAT, "flags": FLAG_EDITED, "value": float(v)}


def prop_bool(name, v):
    return {"name": name, "type": T_BOOL, "flags": FLAG_EDITED, "value": 1 if v else 0}


def prop_string(name, v):
    return {"name": name, "type": T_STRING, "flags": FLAG_EDITED, "value": v}


def prop_array(name, elem_type, values):
    """elem_type: T_OBJECT/T_STRING/T_INT/T_FLOAT/T_BOOL; Object elements are {"formid", "alias", "unused"} dicts
    or plain FormID ints."""
    if elem_type == T_OBJECT:
        values = [x if isinstance(x, dict) else {"formid": x, "alias": ALIAS_NONE, "unused": 0} for x in values]
    return {"name": name, "type": T_ARRAY_BASE + elem_type, "flags": FLAG_EDITED, "value": list(values)}


def script(name, properties=(), flags=0):
    return {"name": name, "flags": flags, "properties": list(properties)}


def quest_alias(alias_id, scripts, quest_formid=0):
    """One alias entry of a QUST VMAD fragment block. The CK writes the owning quest's FormID as the object
    (checked against vanilla in test_vmad_builders); pass it as quest_formid."""
    return {"object": {"formid": quest_formid, "alias": alias_id, "unused": 0}, "version": 6, "obj_format": 2,
            "scripts": list(scripts)}


def vmad_bytes(scripts, rec_sig, aliases=None, extra_version=3):
    """Build a VMAD: record scripts (+ for QUST, alias scripts with no stage fragments)."""
    v = {"version": 6, "obj_format": 2, "scripts": list(scripts), "fragments": None, "tail": b""}
    if aliases:
        if rec_sig != "QUST":
            raise VMADError("aliases only exist on QUST")
        v["fragments"] = {"extra_version": extra_version, "script": script(""), "fragments": [],
                          "aliases": list(aliases)}
    return encode(v, rec_sig)


def property_table(v):
    """Flatten a decoded VMAD into [(owner, script, property, type name)] for checks against .psc declarations.
    owner is "record" or "alias:<id>"."""
    rows = []
    for s in v["scripts"]:
        for p in s["properties"]:
            rows.append(("record", s["name"], p["name"], TYPE_NAMES.get(p["type"], str(p["type"]))))
    fr = v.get("fragments")
    if fr and "aliases" in fr:
        for a in fr["aliases"]:
            for s in a["scripts"]:
                for p in s["properties"]:
                    rows.append(("alias:%d" % a["object"]["alias"], s["name"], p["name"],
                                 TYPE_NAMES.get(p["type"], str(p["type"]))))
    return rows
