"""Build SuperPeasant.esp and SuperPeasant_Test.esp from src/contract.json (CONTRACT.md section 2, agent B2).

Usage (stdlib only; run with -I -B; reads Fallout4.esm read-only, writes only the two contract outputs + a report):
  python -I -B tools/esp/build_esp.py [--contract <contract.json>] [--esm <Fallout4.esm>] [--index <vanilla_index.json>]
        [--out-main build/Data/SuperPeasant.esp] [--out-test build/test/Data/SuperPeasant_Test.esp]
        [--report work/plugin/build_report.json] [--no-p1] [--no-p2] [--only-main] [-q]
With no arguments every path is the contract default (harness/build_all.ps1 step 5 calls it that way).
Exit 0 = both plugins written and every P0 record built; 1 = a P0 record or a self-check failed; 2 = bad input.
Last line: RESULT pass=N fail=M.

How each record is made (contract.json records[].spec is the source of every value):
  * "template" records are byte copies of the vanilla record (esp_writer.template_copy: decompressed, new FormID and
    EDID, version 131, VC info cleared), then patched field by field. Every localized-string subrecord of the template
    (fo4_schema.LSTRING_SIGS) is rewritten inline or dropped (the plugin is not localized).
  * VMAD comes only from contract.json "scripts": every script whose attached_to names the record (quest script,
    alias N, magic effect script). Object property values are (FormID in the numbering of the plugin being built,
    alias ID or -1); an Explosion/P1/P2 target that is not built is left out (CONTRACT section 2, "P1/P2").
  * Decisions this builder adds on top of the contract text (also in build_report.json "decisions"):
      - QUST: Start Game Enabled also sets 0x10 "Starts Enabled" (every SGE quest in Fallout4.esm has both);
        "Run Once" is cleared unless listed. Stages, objectives, conditions and template aliases are dropped.
      - GLOB: FNAM 'f' is written (a global without FNAM is not guaranteed to be float; 0.05 must survive).
      - PACK SP_Wait: the template's location "near linked ref (RELinkGuard)" is replaced by "Near self, radius 0"
        (the spawned peasant has no linked ref; "near self" is what "wait here" means). CONTRACT Changes line.
      - ALCH: DNAM (addiction name, an lstring) is dropped; MGEF DNAM / SPEL+ALCH+PERK DESC become "".
      - PERK: entries are rebuilt from the spec (R4: Mod Falling Damage 0, Mod Incoming Stagger 0, Mod Incoming
        Damage 0.01, Adjust Limb Damage 0; no conditions). NPC_ PRKR entries are written with rank 1 (PRKR_RANK).
      - NPC_: spec voice_type (C247 E1: vanilla EMPTY VTYP) replaces the template's VTCK in place (or goes after
        ACBS/SNAM/INAM when the template has none).
      - EXPL SP_PunchFX: the template's image space modifier (MNAM, frag grenade screen effect) is dropped.
      - SNDR: rebuilt from the spec in vanilla order EDID CNAM GNAM ANAM* ONAM LNAM BNAM (template conditions and
        alternate-sound SNAM dropped); ANAM = spec files[].anam; LNAM = 00 <looping> 00 00 (fo4_schema.SNDR_*).
"""
import argparse
import hashlib
import json
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import esm_reader as er  # noqa: E402
import esp_writer as ew  # noqa: E402
import fo4_schema as S  # noqa: E402
import vmad as vm  # noqa: E402

FO4W = os.path.normpath(os.path.join(HERE, ".."))
DEFAULT_ESM = r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm"
DEFAULTS = {
    "contract": os.path.join(FO4W, "native", "papyrus_scripts", "contract.json"),
    "index": os.path.join(FO4W, "work", "vanilla_index.json"),
    "out_main": os.path.join(FO4W, "build", "Data", "SuperPeasant.esp"),
    "out_test": os.path.join(FO4W, "build", "test", "Data", "SuperPeasant_Test.esp"),
    "report": os.path.join(FO4W, "work", "plugin", "build_report.json"),
}
# Records whose template location input is replaced (see module docstring).
PACK_LOCATION_OVERRIDE = {"SP_Wait": ("Near self", 0, 0), "SP_NativeHold": ("Near self", 0, 0)}  # R6 step 11: SP_NativeHold like SP_Wait
# NPC_ PRKR rank byte: 1 on all 2771 vanilla PRKR entries of Fallout4.esm; 0 means "owns no rank" (R4, durability.md 2).
PRKR_RANK = 1
# Perk entry points: fo4_schema's table plus the two R4 ones of vanilla VRWorkshopShared_ImmuneToDamagePerk 0x0024A3C9
# (DATA 24 01 03 Mod Incoming Damage, 04 01 03 Adjust Limb Damage) [esm]. validate_esp imports these merged tables.
PERK_ENTRY_POINTS = dict(S.PERK_ENTRY_POINTS, **{"Mod Incoming Damage": 36, "Adjust Limb Damage": 4})
PERK_ENTRY_TABS = dict(S.PERK_ENTRY_TABS)
PERK_ENTRY_TABS.update({36: 3, 4: 3})
HEX_RE = re.compile(r"0x[0-9A-Fa-f]{6,8}")


class BuildError(Exception):
    pass


def hx(v):
    return "0x%08X" % v


def zs(s):
    return ew.zstr(s)


# ------------------------------------------------------------------ context
class Ctx(object):
    def __init__(self, contract, index, esm, built_opt):
        self.c = contract
        self.idx = index
        self.esm = esm
        self.decisions = []
        self.notes = {}           # edid -> list of strings
        self.forms = {}           # our edid -> {"plugin", "local", "type", "priority"}
        for r in contract["records"]:
            self.forms[r["edid"]] = {"plugin": r["plugin"], "local": int(r["local_id"], 16), "type": r["type"],
                                     "priority": r["priority"], "spec": r}
        self.built = set()        # our EDIDs actually built (P1/P2 may be missing)
        self.built_opt = built_opt
        self.templates = {}

    def note(self, edid, msg):
        self.notes.setdefault(edid, []).append(msg)

    def masters_of(self, plugin_file):
        if plugin_file == self.c["plugin"]["file"]:
            return list(self.c["plugin"]["masters"])
        if plugin_file == self.c["test_plugin"]["file"]:
            return list(self.c["test_plugin"]["masters"])
        raise BuildError("unknown plugin %s" % plugin_file)

    def num(self, in_plugin, form_file, local):
        """FormID of (form_file, local) in the numbering of in_plugin."""
        masters = self.masters_of(in_plugin)
        if form_file == in_plugin:
            mi = len(masters)
        elif form_file in masters:
            mi = masters.index(form_file)
        else:
            raise BuildError("%s is not a master of %s" % (form_file, in_plugin))
        return (mi << 24) | (local & 0xFFFFFF)

    def ours(self, edid, in_plugin):
        f = self.forms.get(edid)
        if f is None:
            raise BuildError("contract has no record %r" % edid)
        return self.num(in_plugin, f["plugin"], f["local"])

    def vanilla(self, edid, typ=None):
        """FormID of a vanilla form, from contract.vanilla cross-checked with the vanilla index."""
        v = self.c.get("vanilla", {}).get(edid)
        e = self.idx.entry(edid)
        if e is None and v is None:
            raise BuildError("vanilla form %r unknown" % edid)
        fid = int((e or v)["formid"], 16)
        t = (e or v)["type"]
        if v is not None and int(v["formid"], 16) != fid:
            raise BuildError("contract %s %s != vanilla index %s" % (edid, v["formid"], hx(fid)))
        if typ is not None and t != typ:
            raise BuildError("vanilla %s is %s, expected %s" % (edid, t, typ))
        return fid

    def ref(self, name, in_plugin, typ=None):
        """Resolve an EditorID that is either ours or vanilla."""
        if name in self.forms:
            if typ and self.forms[name]["type"] != typ:
                raise BuildError("%s is %s, expected %s" % (name, self.forms[name]["type"], typ))
            return self.ours(name, in_plugin)
        return self.vanilla(name, typ)


# ------------------------------------------------------------------ small helpers
def flags_value(names, table, what):
    v = 0
    for n in names:
        key = re.sub(r"\s*\(.*\)$", "", n).strip()
        if key not in table:
            raise BuildError("unknown %s flag %r" % (what, n))
        v |= table[key]
    return v


def enum_value(name, table, what):
    if name not in table:
        raise BuildError("unknown %s %r" % (what, name))
    return table[name]


def subs_index(rec, sig):
    return rec.index(sig)


def insert_after(rec, after_sigs, new_subs):
    """Insert new_subs after the last subrecord whose sig is in after_sigs (or at the end)."""
    subs = rec.subrecords
    pos = -1
    for i, s in enumerate(subs):
        if s.sig in after_sigs:
            pos = i
    at = pos + 1 if pos >= 0 else len(subs)
    rec.subrecords = subs[:at] + list(new_subs) + subs[at:]


def insert_before(rec, before_sig, new_subs, fallback_after=()):
    subs = rec.subrecords
    i = rec.index(before_sig)
    if i < 0:
        insert_after(rec, fallback_after, new_subs)
        return
    rec.subrecords = subs[:i] + list(new_subs) + subs[i:]


def drop(rec, *sigs):
    return ew.remove_subs(rec, *sigs)


def parse_remove_list(items):
    """contract 'remove' entries like 'CNTO/COCT (no inventory)' or 'MO2T/MO3T texture hashes' -> signatures."""
    out = []
    for it in items:
        head = it.split(" ")[0]
        for tok in head.split("/"):
            if re.fullmatch(r"[A-Z0-9_]{4}", tok):
                out.append(tok)
    return out


def set_lstrings(ctx, rec, edid, values):
    """Rewrite every localized-string subrecord of the record type: values[sig] = text (inline) or None (drop).
    Signatures not in values become "" if present. Returns the list of actions."""
    acts = []
    for sig in S.LSTRING_SIGS.get(rec.sig, []):
        present = rec.get_all(sig)
        if sig in values:
            if values[sig] is None:
                if present:
                    drop(rec, sig)
                    acts.append("%s dropped" % sig)
            else:
                if len(present) > 1:
                    raise BuildError("%s has %d %s subrecords" % (edid, len(present), sig))
                ew.set_sub(rec, sig, zs(values[sig]))
                acts.append("%s=%r" % (sig, values[sig]))
        elif present:
            for s in present:
                s.data = zs("")
            acts.append("%s -> \"\"" % sig)
    return acts


# ------------------------------------------------------------------ VMAD
def script_entries(ctx, edid):
    """Contract scripts attached to record `edid`: (slot, alias_id or None, script name, script dict)."""
    out = []
    for name, s in ctx.c["scripts"].items():
        at = s.get("attached_to")
        if not at or at.get("record") != edid:
            continue
        slot = at.get("slot", "")
        m = re.match(r"alias (\d+)", slot)
        out.append((slot, int(m.group(1)) if m else None, name, s))
    return out


def prop_vmad(ctx, prop, in_plugin, owner):
    """One contract property -> VMAD property dict (or None if its target is an unbuilt P1/P2 record)."""
    ptype = prop["type"]
    val = prop["value"]
    name = prop["name"]
    base = ptype[:-2] if ptype.endswith("[]") else ptype
    if base.lower() in S.PAPYRUS_PRIMITIVES:
        t = {"int": vm.T_INT, "float": vm.T_FLOAT, "bool": vm.T_BOOL, "string": vm.T_STRING}[base.lower()]
        raw = val.get("value") if isinstance(val, dict) else val
        if ptype.endswith("[]"):
            return vm.prop_array(name, t, list(raw))
        return {vm.T_INT: vm.prop_int, vm.T_FLOAT: vm.prop_float, vm.T_BOOL: vm.prop_bool,
                vm.T_STRING: vm.prop_string}[t](name, raw)
    if ptype.endswith("[]"):
        vals = val if isinstance(val, list) else val.get("values", [])
        objs = []
        for v in vals:
            fid, alias = object_value(ctx, v, in_plugin, owner, name)
            if fid is None:
                continue
            objs.append({"formid": fid, "alias": alias, "unused": 0})
        return vm.prop_array(name, vm.T_OBJECT, objs)
    fid, alias = object_value(ctx, val, in_plugin, owner, name)
    if fid is None:
        return None
    return vm.prop_object(name, fid, alias)


def object_value(ctx, val, in_plugin, owner, pname):
    kind = val.get("kind")
    if kind == "alias":
        q = val["quest"]
        fid = ctx.ours(q, in_plugin)
        want = int(val["quest_formid"], 16)
        if fid != want:
            raise BuildError("%s.%s: quest %s is %s here, contract says %s" % (owner, pname, q, hx(fid), hx(want)))
        alias_ids = [a["id"] for a in ctx.forms[q]["spec"]["spec"].get("aliases", [])]
        if val["alias_id"] not in alias_ids:
            raise BuildError("%s.%s: %s has no alias %d" % (owner, pname, q, val["alias_id"]))
        return fid, int(val["alias_id"])
    if kind == "ours":
        ed = val["edid"]
        if ed not in ctx.forms:
            raise BuildError("%s.%s: %s is not a contract record" % (owner, pname, ed))
        # P0 records are always built (a missing one fails the build); optional ones are built first (TYPE_ORDER)
        if ctx.forms[ed]["priority"] != "P0" and ed not in ctx.built:
            ctx.decisions.append("%s.%s left out of the VMAD: %s (%s) is not built" %
                                 (owner, pname, ed, ctx.forms[ed]["priority"]))
            return None, None
        fid = ctx.ours(ed, in_plugin)
        if "formid" in val and int(val["formid"], 16) != fid:
            raise BuildError("%s.%s: %s is %s in %s, contract says %s" %
                             (owner, pname, ed, hx(fid), in_plugin, val["formid"]))
        return fid, vm.ALIAS_NONE
    if kind == "vanilla":
        fid = ctx.vanilla(val["edid"], val.get("type"))
        if int(val["formid"], 16) != fid:
            raise BuildError("%s.%s: vanilla %s mismatch" % (owner, pname, val["edid"]))
        return fid, vm.ALIAS_NONE
    raise BuildError("%s.%s: unknown value kind %r" % (owner, pname, kind))


def build_script(ctx, script_name, s, in_plugin):
    props = []
    for p in s.get("properties", []):
        v = prop_vmad(ctx, p, in_plugin, script_name)
        if v is not None:
            props.append(v)
    return vm.script(script_name, props)


def build_vmad(ctx, rec, edid, in_plugin):
    entries = script_entries(ctx, edid)
    rec_scripts, alias_scripts = [], {}
    for slot, alias_id, name, s in entries:
        sc = build_script(ctx, name, s, in_plugin)
        if alias_id is None:
            rec_scripts.append(sc)
        else:
            alias_scripts.setdefault(alias_id, []).append(sc)
    if not rec_scripts and not alias_scripts:
        return None
    aliases = None
    if alias_scripts:
        if rec.sig != "QUST":
            raise BuildError("alias scripts on a %s" % rec.sig)
        aliases = [vm.quest_alias(a, alias_scripts[a], rec.formid) for a in sorted(alias_scripts)]
    return vm.vmad_bytes(rec_scripts, rec.sig, aliases)


# ------------------------------------------------------------------ record builders
def b_qust(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    fid = ctx.ours(edid, plugin)
    r = ew.template_copy(tpl, fid, edid)
    keep = {"EDID", "DNAM", "NEXT"}
    dropped = sorted({s.sig for s in r.subrecords if s.sig not in keep})
    dnam = bytearray(r.get("DNAM").data)
    flags = struct.unpack_from("<H", dnam, 0)[0]
    t_flags = flags
    want = flags_value(sp.get("flags", []), S.QUST_FLAGS, "quest")
    if want & S.QUST_FLAGS["Start Game Enabled"]:
        want |= S.QUST_SGE_COMPANION
    clear = flags_value(sp.get("not_flags", []), S.QUST_FLAGS, "quest")
    if "Run Once" not in sp.get("flags", []):
        clear |= S.QUST_FLAGS["Run Once"]
    flags = (flags | want) & ~clear & 0xFFFF
    struct.pack_into("<H", dnam, 0, flags)
    if "priority" in sp:
        dnam[S.QUST_DNAM_PRIORITY_OFS] = int(sp["priority"])
    subs = [er.Subrecord("EDID", zs(edid))]
    vm_bytes = build_vmad(ctx, r, edid, plugin)
    if vm_bytes is not None:
        subs.append(er.Subrecord("VMAD", vm_bytes))
    if sp.get("full_name"):
        subs.append(er.Subrecord("FULL", zs(sp["full_name"])))
    subs.append(er.Subrecord("DNAM", bytes(dnam)))
    subs.append(er.Subrecord("NEXT", b""))
    aliases = sp.get("aliases", [])
    next_alias = sp.get("next_alias_id", (max([a["id"] for a in aliases]) + 1) if aliases else 0)
    subs.append(er.Subrecord("ANAM", struct.pack("<I", next_alias)))
    for a in aliases:
        if a.get("kind", "ReferenceAlias") != "ReferenceAlias":
            raise BuildError("%s alias %s: only ReferenceAlias is supported" % (edid, a["name"]))
        subs.append(er.Subrecord("ALST", struct.pack("<I", a["id"])))
        subs.append(er.Subrecord("ALID", zs(a["name"])))
        subs.append(er.Subrecord("FNAM", struct.pack("<I", flags_value(a.get("flags", []), S.ALIAS_FLAGS,
                                                                           "alias"))))
        fill = a.get("fill", "")
        if fill.lower().startswith("forced reference"):
            m = HEX_RE.search(fill)
            if not m:
                raise BuildError("%s alias %s: forced reference without a FormID" % (edid, a["name"]))
            ref = int(m.group(0), 16)
            if ref not in S.ENGINE_FORMS and ctx.idx.type_of(ref) is None:
                raise BuildError("%s alias %s: forced ref %s unknown" % (edid, a["name"], hx(ref)))
            subs.append(er.Subrecord("ALFR", struct.pack("<I", ref)))
        elif not fill.lower().startswith("none"):
            raise BuildError("%s alias %s: unsupported fill %r" % (edid, a["name"], fill))
        for p in a.get("packages", []):
            subs.append(er.Subrecord("ALPC", struct.pack("<I", ctx.ref(p, plugin, "PACK"))))
        subs.append(er.Subrecord("VTCK", struct.pack("<I", 0)))
        subs.append(er.Subrecord("ALED", b""))
    r.subrecords = subs
    ctx.note(edid, "DNAM flags %s -> %s, priority %d; template subrecords dropped: %s" %
             (hex(t_flags), hex(flags), dnam[2], ",".join(dropped)))
    return r


def b_npc(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    rem = set(parse_remove_list(sp.get("remove", []))) | {"VMAD", "TPLT", "TPTA", "LTPT", "CNTO", "COCT", "PKID"}
    gone = [s.sig for s in r.subrecords if s.sig in rem]
    drop(r, *rem)
    acbs = bytearray(r.get("ACBS").data)
    fl = struct.unpack_from("<I", acbs, 0)[0]
    fl0 = fl
    fl |= flags_value(sp.get("acbs_flags_set", []), S.ACBS_FLAGS, "ACBS")
    fl &= ~flags_value(sp.get("acbs_flags_clear", []), S.ACBS_FLAGS, "ACBS")
    struct.pack_into("<I", acbs, 0, fl)
    if "level" in sp:
        struct.pack_into("<h", acbs, S.ACBS_LEVEL_OFS, int(sp["level"]))
    struct.pack_into("<H", acbs, S.ACBS_TEMPLATE_FLAGS_OFS, 0)   # no TPLT -> use no template data
    ew.set_sub(r, "ACBS", bytes(acbs))
    # factions: replace all SNAM at the position of the first one
    facs = [er.Subrecord("SNAM", struct.pack("<Ib", ctx.ref(f["faction"], plugin, "FACT"), int(f["rank"])))
            for f in sp.get("factions", [])]
    first = r.index("SNAM")
    drop(r, "SNAM")
    if first < 0:
        first = r.index("ACBS") + 1
    r.subrecords = r.subrecords[:first] + facs + r.subrecords[first:]
    ew.set_sub(r, "RNAM", struct.pack("<I", ctx.ref(sp["race"], plugin, "RACE")))
    aidt = bytearray(r.get("AIDT").data)
    ai = sp.get("ai_data", {})
    if "aggression" in ai:
        aidt[S.AIDT_OFS["aggression"]] = enum_value(ai["aggression"], S.AIDT_AGGRESSION, "aggression")
    if "confidence" in ai:
        aidt[S.AIDT_OFS["confidence"]] = enum_value(ai["confidence"], S.AIDT_CONFIDENCE, "confidence")
    if "assistance" in ai:
        aidt[S.AIDT_OFS["assistance"]] = enum_value(ai["assistance"], S.AIDT_ASSISTANCE, "assistance")
    ew.set_sub(r, "AIDT", bytes(aidt))
    vt0 = None
    if sp.get("voice_type"):
        # C247 E1: voice type (VTYP). Replaced in place; if the template had none it goes after the factions/death
        # item, where xEdit's FO4 NPC_ order has it (ACBS, SNAM*, INAM, VTCK, ...).
        old = r.get("VTCK")
        vt0 = hx(struct.unpack("<I", old.data)[0]) if old is not None else "none"
        vt = struct.pack("<I", ctx.ref(sp["voice_type"], plugin, "VTYP"))
        if old is not None:
            ew.set_sub(r, "VTCK", vt)
        else:
            insert_after(r, ("ACBS", "SNAM", "INAM"), [er.Subrecord("VTCK", vt)])
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "SHRT": None, "ATTX": None})
    if vt0 is not None:
        acts.append("VTCK %s -> %s" % (vt0, sp["voice_type"]))
    if sp.get("default_outfit"):
        ew.set_sub(r, "DOFT", struct.pack("<I", ctx.ref(sp["default_outfit"], plugin, "OTFT")))
    perks = []
    for p in sp.get("perks", []):
        pe = p.split(" ")[0]
        if pe in ctx.built:
            perks.append(ctx.ref(pe, plugin, "PERK"))
        else:
            ctx.decisions.append("%s: perk %s not listed (record not built)" % (edid, pe))
    drop(r, "PRKZ", "PRKR")
    if perks:
        # PRKR rank = owned ranks, counted from 1: every vanilla NPC_ PRKR (2771/2771) has rank 1. Rank 0 = the perk
        # is NOT owned (HasPerk False, no entry point runs: run 20261009-055623, work\r3\durability.md 2).
        new = [er.Subrecord("PRKZ", struct.pack("<I", len(perks)))] + \
              [er.Subrecord("PRKR", struct.pack("<IB", p, PRKR_RANK)) for p in perks]
        insert_before(r, "PRPS", new, fallback_after=("RNAM",))
    kws = [ctx.ref(k, plugin, "KYWD") for k in sp.get("keywords", [])]
    if kws:
        have = r.get("KWDA")
        old_k = list(struct.unpack("<%dI" % (len(have.data) // 4), have.data)) if have is not None else []
        allk = old_k + [k for k in kws if k not in old_k]
        drop(r, "KSIZ", "KWDA")
        insert_after(r, ("AIDT", "PKID"), [er.Subrecord("KSIZ", struct.pack("<I", len(allk))),
                                           er.Subrecord("KWDA", struct.pack("<%dI" % len(allk), *allk))])
        acts.append("keywords %s" % [hx(k) for k in allk])
    ctx.note(edid, "ACBS flags %s -> %s; dropped %s; %s; perks %s" %
             (hex(fl0), hex(fl), ",".join(gone), "; ".join(acts), [hx(p) for p in perks]))
    return r


def b_arma(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    gone = [s.sig for s in r.subrecords if s.sig in S.ARMA_MODEL_FAMILY or s.sig == "MODL"]
    drop(r, *(S.ARMA_MODEL_FAMILY + ("MODL",)))
    if len(r.get("BOD2").data) != 4:
        raise BuildError("%s: template BOD2 is not 4 bytes" % edid)
    ew.set_sub(r, "BOD2", struct.pack("<I", int(sp["bod2_value"], 16)))
    ew.set_sub(r, "RNAM", struct.pack("<I", ctx.ref(sp["race"], plugin, "RACE")))
    models = [er.Subrecord("MOD2", zs(sp["male_model"]))]
    if sp.get("female_model"):
        models.append(er.Subrecord("MOD3", zs(sp["female_model"])))
    insert_after(r, ("DNAM",), models)
    for a in sp.get("additional_races", []):
        r.subrecords.append(er.Subrecord("MODL", struct.pack("<I", ctx.ref(a, plugin, "RACE"))))
    ctx.note(edid, "dropped %s" % ",".join(gone))
    return r


def b_armo(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    if r.get("OBTE") is not None:
        raise BuildError("%s: template has an object-template block (lstrings inside); pick another" % edid)
    rem = set(parse_remove_list(sp.get("remove", []))) | {"EITM", "PTRN", "KSIZ", "KWDA", "DESC"}
    gone = [s.sig for s in r.subrecords if s.sig in rem]
    drop(r, *rem)
    if any("Non-Playable" in f for f in sp.get("record_flags", [])):
        r.flags |= S.ARMO_FLAG_NON_PLAYABLE
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "DESC": None})
    ew.set_sub(r, "BOD2", struct.pack("<I", int(sp["bod2_value"], 16)))
    ew.set_sub(r, "RNAM", struct.pack("<I", ctx.ref(sp["race"], plugin, "RACE")))
    first = r.index("INDX")
    drop(r, "INDX", "MODL")
    arm = []
    for a in sp["armature"]:
        arm.append(er.Subrecord("INDX", struct.pack("<H", int(a["index"]))))
        arm.append(er.Subrecord("MODL", struct.pack("<I", ctx.ref(a["arma"], plugin, "ARMA"))))
    if first < 0:
        first = r.index("DATA")
    r.subrecords = r.subrecords[:first] + arm + r.subrecords[first:]
    ew.set_sub(r, "DATA", struct.pack("<ifI", int(sp.get("value", 0)), float(sp.get("weight", 0.0)),
                                      int(sp.get("health", 0))))
    ctx.note(edid, "dropped %s; %s" % (",".join(gone), "; ".join(acts)))
    return r


def b_otft(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    ew.set_sub(r, "INAM", b"".join(struct.pack("<I", ctx.ref(i, plugin)) for i in sp["items"]))
    return r


def b_pack(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    drop(r, "VMAD")
    # top-level conditions sit between PSDT and PKCU (2169 vanilla packages) [esm]; procedure-tree CTDAs come later
    subs = r.subrecords
    pk = r.index("PKCU")
    if pk < 0:
        raise BuildError("%s: template has no PKCU" % edid)
    head = [s for s in subs[:pk] if s.sig not in ("CTDA", "CIS1", "CIS2", "CITC")]
    conds = []
    for c in sp.get("conditions", []):
        if c["function"] != "GetGlobalValue":
            raise BuildError("%s: only GetGlobalValue conditions are supported" % edid)
        conds.append(er.Subrecord("CTDA", S.ctda_bytes(S.FUNC_GET_GLOBAL_VALUE, ctx.ref(c["global"], plugin, "GLOB"),
                                                       c.get("op", "=="), float(c["value"]),
                                                       c.get("run_on", "Subject"))))
    r.subrecords = head + conds + subs[pk:]
    if edid in PACK_LOCATION_OVERRIDE:
        tname, value, radius = PACK_LOCATION_OVERRIDE[edid]
        plds = [i for i, s in enumerate(r.subrecords) if s.sig == "PLDT"]
        if len(plds) != 1:
            raise BuildError("%s: expected one PLDT in the template, found %d" % (edid, len(plds)))
        old = S.PLDT.unpack(r.subrecords[plds[0]].data)
        r.subrecords[plds[0]].data = S.PLDT.pack(S.PLDT_TYPES[tname], value, radius, old[3])
        ctx.decisions.append("%s: package location %s -> %s (type %d), radius %d -> %d" %
                             (edid, old[:2], tname, S.PLDT_TYPES[tname], old[2], radius))
    ctx.note(edid, "%d condition(s)" % len(conds))
    return r


def b_mgef(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    gone = [s.sig for s in r.subrecords if s.sig in ("VMAD", "CTDA", "CIS1", "CIS2")]
    drop(r, "VMAD", "CTDA", "CIS1", "CIS2")
    vmb = build_vmad(ctx, r, edid, plugin)
    if vmb is not None:
        r.subrecords.insert(1, er.Subrecord("VMAD", vmb))
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "DNAM": ""})
    d = bytearray(r.get("DATA").data)
    if len(d) != 152:
        raise BuildError("%s: MGEF DATA is %d bytes, expected 152" % (edid, len(d)))
    if "flags" in sp:
        fv = flags_value(sp["flags"], S.MGEF_FLAGS, "MGEF")
        inf = sp.get("flags_value_informative")
        if inf:
            m = HEX_RE.search(inf)
            if m and int(m.group(0), 16) != fv:
                raise BuildError("%s: flags %s != contract informative %s" % (edid, hex(fv), m.group(0)))
        struct.pack_into("<I", d, S.MGEF_OFS["flags"], fv)
    if "archetype" in sp:
        struct.pack_into("<I", d, S.MGEF_OFS["archetype"], enum_value(sp["archetype"], S.MGEF_ARCHETYPE, "archetype"))
    if "casting_type" in sp:
        struct.pack_into("<I", d, S.MGEF_OFS["casting_type"],
                         enum_value(sp["casting_type"], S.CASTING_TYPE, "casting type"))
    if "delivery" in sp:
        struct.pack_into("<I", d, S.MGEF_OFS["delivery"], enum_value(sp["delivery"], S.DELIVERY, "delivery"))
    ew.set_sub(r, "DATA", bytes(d))
    ctx.note(edid, "flags %s; dropped %s; %s" % (hex(struct.unpack_from("<I", d)[0]), ",".join(gone),
                                                  "; ".join(acts)))
    return r


def effects_subs(ctx, effects, plugin):
    out = []
    for e in effects:
        out.append(er.Subrecord("EFID", struct.pack("<I", ctx.ref(e["mgef"], plugin, "MGEF"))))
        out.append(er.Subrecord("EFIT", S.EFIT.pack(float(e.get("magnitude", 0.0)), int(e.get("area", 0)),
                                                    int(e.get("duration", 0)))))
    return out


def replace_effects(ctx, r, effects, plugin):
    first = r.index("EFID")
    subs = r.subrecords
    if first < 0:
        first = len(subs)
    tail = [s for s in subs[first:] if s.sig not in ("EFID", "EFIT", "CTDA", "CIS1", "CIS2")]
    r.subrecords = subs[:first] + effects_subs(ctx, effects, plugin) + tail


def b_spel(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "DESC": ""})
    cost, fl, typ, charge, cast, target, dur, rng, perk = S.SPIT.unpack(r.get("SPIT").data)
    cost = int(sp.get("cost", cost))
    if "flags" in sp:
        fl = flags_value(sp["flags"], S.SPIT_FLAGS, "spell")
    if "spell_type" in sp:
        typ = enum_value(sp["spell_type"], S.SPELL_TYPE, "spell type")
    if "casting_type" in sp:
        cast = enum_value(sp["casting_type"], S.CASTING_TYPE, "casting type")
    if "delivery" in sp:
        target = enum_value(sp["delivery"], S.DELIVERY, "delivery")
    ew.set_sub(r, "SPIT", S.SPIT.pack(cost, fl, typ, charge, cast, target, dur, rng, perk))
    replace_effects(ctx, r, sp.get("effects", []), plugin)
    ctx.note(edid, "SPIT flags %s; %s" % (hex(fl), "; ".join(acts)))
    return r


def b_alch(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "DESC": "", "DNAM": None})
    if "weight" in sp:
        ew.set_sub(r, "DATA", struct.pack("<f", float(sp["weight"])))
    val, fl, addict, chance, sound = S.ENIT.unpack(r.get("ENIT").data)
    if "value" in sp:
        val = int(sp["value"])
    ew.set_sub(r, "ENIT", S.ENIT.pack(val, fl, addict, chance, sound))
    replace_effects(ctx, r, sp.get("effects", []), plugin)
    ctx.note(edid, "; ".join(acts))
    return r


def b_mesg(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    drop(r, "ITXT", "CTDA", "CIS1", "CIS2")
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("title"), "DESC": sp.get("text"), "NNAM": None})
    dn = bytearray(r.get("DNAM").data)
    f = struct.unpack_from("<I", dn)[0]
    f = (f | S.MESG_FLAG_MESSAGE_BOX) if sp.get("message_box", True) else (f & ~S.MESG_FLAG_MESSAGE_BOX)
    struct.pack_into("<I", dn, 0, f)
    ew.set_sub(r, "DNAM", bytes(dn))
    idx = sp.get("button_index", {})
    bconds = sp.get("button_conditions", {})
    unknown = [b for b in bconds if b not in sp["buttons"]]
    if unknown:
        raise BuildError("%s: button_conditions for unknown button(s) %s" % (edid, unknown))
    ncond = 0
    for i, b in enumerate(sp["buttons"]):
        if idx and idx.get(b) != i:
            raise BuildError("%s: button %r index %s != list position %d" % (edid, b, idx.get(b), i))
        r.subrecords.append(er.Subrecord("ITXT", zs(b)))
        # CARRY: a button's conditions follow its ITXT (vanilla MESG layout, e.g. FollowersCompanionMenuMain button 0)
        for c in bconds.get(b, []):
            r.subrecords.append(er.Subrecord("CTDA", mesg_ctda(ctx, edid, c, plugin)))
            ncond += 1
    ctx.note(edid, "%d buttons, %d button condition(s); %s" % (len(sp["buttons"]), ncond, "; ".join(acts)))
    return r


def mesg_ctda(ctx, edid, c, plugin):
    """One MESG button condition (CARRY): GetGlobalValue(<our GLOB>) <op> value, the same encoder as b_pack."""
    if c.get("function") != "GetGlobalValue":
        raise BuildError("%s: only GetGlobalValue button conditions are supported" % edid)
    return S.ctda_bytes(S.FUNC_GET_GLOBAL_VALUE, ctx.ref(c["global"], plugin, "GLOB"), c.get("op", "=="),
                        float(c["value"]), c.get("run_on", "Subject"))


def b_flst(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    subs = [er.Subrecord("EDID", zs(edid))]
    for e in sp["entries"]:
        subs.append(er.Subrecord("LNAM", struct.pack("<I", ctx.ref(e, plugin))))
    return ew.new_record("FLST", ctx.ours(edid, plugin), subs)


def b_glob(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    r.flags &= ~S.GLOB_FLAG_CONSTANT
    vt = sp.get("value_type", "float")
    if vt != "float":
        raise BuildError("%s: only float globals are supported" % edid)
    drop(r, "FNAM")
    insert_after(r, ("EDID",), [er.Subrecord("FNAM", b"f")])
    ew.set_sub(r, "FLTV", struct.pack("<f", float(sp["value"])))
    return r


def b_perk(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    drop(r, "VMAD")
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name"), "DESC": "", "EPF2": None})
    first = r.index("PRKE")
    subs = r.subrecords
    if first < 0:
        first = len(subs)
    head = subs[:first]
    # PERK DATA (the first DATA, before any PRKE) = trait level ranks playable hidden
    for s in head:
        if s.sig == "DATA":
            d = bytearray(s.data)
            if "playable" in sp:
                d[3] = 1 if sp["playable"] else 0
            if "hidden" in sp:
                d[4] = 1 if sp["hidden"] else 0
            s.data = bytes(d)
            break
    entries = []
    for i, text in enumerate(sp.get("entry_points", [])):
        m = re.match(r"\s*([^:]+):\s*(Set Value|Add Value|Multiply Value)\s+(-?[0-9.]+)", text)
        if not m:
            raise BuildError("%s: cannot read entry point %r" % (edid, text))
        ep = enum_value(m.group(1).strip(), PERK_ENTRY_POINTS, "entry point")
        fn = S.PERK_FUNCTIONS[m.group(2)]
        entries += [er.Subrecord("PRKE", bytes([2, 0, 0])),
                    er.Subrecord("DATA", bytes([ep, fn, PERK_ENTRY_TABS[ep]])),
                    er.Subrecord("EPFT", b"\x01"),
                    er.Subrecord("EPFB", struct.pack("<H", i)),
                    er.Subrecord("EPFD", struct.pack("<f", float(m.group(3)))),
                    er.Subrecord("PRKF", b"")]
    r.subrecords = head + entries
    ctx.note(edid, "%d entry points; %s" % (len(sp.get("entry_points", [])), "; ".join(acts)))
    return r


def b_expl(ctx, spec_rec, tpl, plugin):
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    acts = set_lstrings(ctx, r, edid, {"FULL": sp.get("full_name")})
    # The contract wants "sound/particles only". The template's MNAM is ExplosionGrenadeFragImod, a full-screen image
    # space effect. With up to 7.5 punches a second it must never reach the player's screen, so it is dropped rather
    # than relying on IS Radius 0 (reviewer, 2026-10-09).
    if r.get("MNAM") is not None:
        drop(r, "MNAM")
        ctx.decisions.append("%s: template image space modifier (MNAM) dropped: contract says sound/particles only"
                             % edid)
    d = bytearray(r.get("DATA").data)
    if len(d) != 84:
        raise BuildError("%s: EXPL DATA is %d bytes, expected 84" % (edid, len(d)))
    O = S.EXPL_OFS
    if "force" in sp:
        struct.pack_into("<f", d, O["force"], float(sp["force"]))
    if "damage" in sp:
        struct.pack_into("<f", d, O["damage"], float(sp["damage"]))
    if "radius" in sp:
        for k in ("inner_radius", "outer_radius", "is_radius"):
            struct.pack_into("<f", d, O[k], float(sp["radius"]))
    if sp.get("knock_down") == "Never":
        f = struct.unpack_from("<I", d, O["flags"])[0]
        struct.pack_into("<I", d, O["flags"], f & ~(S.EXPL_FLAG_KNOCKDOWN_ALWAYS | S.EXPL_FLAG_KNOCKDOWN_FORMULA))
    if "stagger" in sp:
        struct.pack_into("<I", d, O["stagger"], enum_value(sp["stagger"], S.EXPL_STAGGER, "stagger"))
    if "sound1" in sp:                       # STEP 7.6 (optional): the explosion's own sound (0 = none)
        struct.pack_into("<I", d, O["sound1"], int(sp["sound1"]))
    if sp.get("drop_enchant") and r.get("EITM") is not None:   # STEP 7.6 (optional): no object effect on anyone
        drop(r, "EITM")
        ctx.decisions.append("%s: template enchantment (EITM) dropped: pure visual effect" % edid)
    ew.set_sub(r, "DATA", bytes(d))
    ctx.note(edid, "; ".join(acts) or "DATA patched")
    return r


def sndr_bnam(sp):
    """contract SNDR spec -> BNAM bytes (fo4_schema.SNDR_BNAM: shift%, variance%, priority, dB variance, dB*100)."""
    att = int(round(float(sp.get("static_attenuation_db", 0.0)) * 100))
    vals = (int(sp.get("frequency_shift_pct", 0)), int(sp.get("frequency_variance_pct", 0)), int(sp.get("priority", 128)),
            int(sp.get("db_variance", 0)), att)
    if not (-128 <= vals[0] <= 127 and -128 <= vals[1] <= 127 and 0 <= vals[2] <= 255 and 0 <= vals[3] <= 255
            and 0 <= att <= 0xFFFF):
        raise BuildError("SNDR BNAM value out of range: %r" % (vals,))
    return S.SNDR_BNAM.pack(*vals)


def b_sndr(ctx, spec_rec, tpl, plugin):
    """Standard sound descriptor: template byte copy, then rebuilt in the vanilla subrecord order
    EDID CNAM GNAM ANAM* ONAM LNAM BNAM from the spec (template CTDA / SNAM / ANAM dropped)."""
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    r = ew.template_copy(tpl, ctx.ours(edid, plugin), edid)
    want_type = S.SNDR_CNAM[sp.get("descriptor_type", "Standard")]
    if want_type != S.SNDR_CNAM["Standard"]:
        raise BuildError("%s: only Standard sound descriptors are supported" % edid)
    cn = r.get("CNAM")
    if cn is None or struct.unpack("<I", cn.data)[0] != want_type:
        raise BuildError("%s: template %s is not a Standard descriptor" % (edid, tpl.edid))
    files = sp.get("files", [])
    if not files:
        raise BuildError("%s: no sound files" % edid)
    loop = enum_value(sp.get("looping", "None"), S.SNDR_LOOPING, "looping")
    gone = sorted({s.sig for s in r.subrecords if s.sig not in ("EDID", "CNAM", "GNAM", "ANAM", "ONAM", "LNAM",
                                                                  "BNAM")})
    subs = [er.Subrecord("EDID", zs(edid)), er.Subrecord("CNAM", struct.pack("<I", want_type)),
            er.Subrecord("GNAM", struct.pack("<I", ctx.ref(sp["category"], plugin, "SNCT")))]
    for f in files:
        path = f["anam"]
        if not path.lower().endswith(".wav") or not path.isascii() or " " in path:
            raise BuildError("%s: bad sound path %r" % (edid, path))
        subs.append(er.Subrecord("ANAM", zs(path)))
    subs.append(er.Subrecord("ONAM", struct.pack("<I", ctx.ref(sp["output_model"], plugin, "SOPM"))))
    subs.append(er.Subrecord("LNAM", S.SNDR_LNAM.pack(0, loop, 0, 0)))
    subs.append(er.Subrecord("BNAM", sndr_bnam(sp)))
    r.subrecords = subs
    ctx.note(edid, "%d file(s), category %s, output %s, looping %s; template subrecords dropped: %s" %
             (len(files), sp["category"], sp["output_model"], sp.get("looping", "None"), ",".join(gone) or "none"))
    return r


def b_kywd(ctx, spec_rec, tpl, plugin):
    """R6 step 6: a plain keyword (EDID only, like vanilla ActorTypeNPC)."""
    edid = spec_rec["edid"]
    return ew.new_record("KYWD", ctx.ours(edid, plugin), [er.Subrecord("EDID", zs(edid))])


def b_dfob(ctx, spec_rec, tpl, plugin):
    """R6 step 6: a default object (EDID = the engine's default-object name, DATA = the form), like vanilla DFOBs."""
    edid, sp = spec_rec["edid"], spec_rec["spec"]
    form = ctx.ref(sp["form"], plugin)
    ctx.note(edid, "DATA -> %s (%s)" % (sp["form"], hx(form)))
    return ew.new_record("DFOB", ctx.ours(edid, plugin), [er.Subrecord("EDID", zs(edid)), er.Subrecord("DATA", struct.pack("<I", form))])


BUILDERS = {"KYWD": b_kywd, "DFOB": b_dfob, "QUST": b_qust, "NPC_": b_npc, "ARMA": b_arma, "ARMO": b_armo, "OTFT": b_otft, "PACK": b_pack,
            "MGEF": b_mgef, "SPEL": b_spel, "ALCH": b_alch, "MESG": b_mesg, "FLST": b_flst, "GLOB": b_glob,
            "PERK": b_perk, "EXPL": b_expl, "SNDR": b_sndr}
# Build order: records other records point at come first (P1/P2 decide whether NPC perks / the FX property exist).
# SNDR first: script properties (Sound) may point at them.
TYPE_ORDER = ["KYWD", "DFOB", "SNDR", "GLOB", "FLST", "PACK", "ARMA", "ARMO", "OTFT", "MESG", "EXPL", "PERK", "MGEF", "SPEL", "ALCH",
              "NPC_", "QUST"]


# ------------------------------------------------------------------ plugins
def build_plugin(ctx, plugin_file, header):
    recs = [r for r in ctx.c["records"] if r["plugin"] == plugin_file]
    out = {}
    fails = []
    order = sorted(recs, key=lambda r: (TYPE_ORDER.index(r["type"]), int(r["local_id"], 16)))
    for spec_rec in order:
        edid, pri = spec_rec["edid"], spec_rec["priority"]
        if pri != "P0" and not ctx.built_opt.get(pri, True):
            ctx.decisions.append("%s (%s) skipped by option" % (edid, pri))
            continue
        tpl = None
        t = spec_rec.get("template")
        if t:
            tpl = ctx.templates.get(int(t["formid"], 16))
            if tpl is None or tpl.sig != t["type"] or tpl.edid != t["edid"]:
                raise BuildError("%s: template %s %s not found in Fallout4.esm" % (edid, t["edid"], t["formid"]))
        try:
            rec = BUILDERS[spec_rec["type"]](ctx, spec_rec, tpl, plugin_file)
        except BuildError as e:
            if pri == "P0":
                fails.append("%s: %s" % (edid, e))
                continue
            ctx.decisions.append("%s (%s) NOT built: %s" % (edid, pri, e))
            continue
        want = int(spec_rec["file_formid"], 16)
        if rec.formid != want:
            fails.append("%s: FormID %s != contract %s" % (edid, hx(rec.formid), hx(want)))
        out[edid] = rec
        ctx.built.add(edid)
    meta = ctx.c["plugin"] if plugin_file == ctx.c["plugin"]["file"] else ctx.c["test_plugin"]
    p = ew.new_plugin(meta["masters"], author=header.get("author", ctx.c["plugin"].get("author", "SuperPeasant")),
                      description=header.get("description"), next_object_id=int(meta["next_object_id"], 16))
    for spec_rec in sorted(recs, key=lambda r: int(r["local_id"], 16)):
        if spec_rec["edid"] in out:
            ew.add_record(p, out[spec_rec["edid"]])
    return p, out, fails


def load_templates(ctx):
    want = {}
    for r in ctx.c["records"]:
        t = r.get("template")
        if t:
            want[int(t["formid"], 16)] = t["type"]
    ctx.templates = er.get_records(ctx.esm, list(want), set(want.values()))


def sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        h.update(f.read())
    return h.hexdigest()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--contract", default=DEFAULTS["contract"])
    ap.add_argument("--esm", default=DEFAULT_ESM)
    ap.add_argument("--index", default=DEFAULTS["index"])
    ap.add_argument("--out-main", default=DEFAULTS["out_main"])
    ap.add_argument("--out-test", default=DEFAULTS["out_test"])
    ap.add_argument("--report", default=DEFAULTS["report"])
    ap.add_argument("--no-p1", action="store_true", help="do not build P1 records (none since R4: SP_PerkImmortal is P0)")
    ap.add_argument("--no-p2", action="store_true", help="do not build P2 records (SP_PunchFX)")
    ap.add_argument("--only-main", action="store_true", help="skip SuperPeasant_Test.esp")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args(argv)

    for p in (a.contract, a.esm, a.index):
        if not os.path.isfile(p):
            print("ERROR missing input: %s" % p)
            print("RESULT pass=0 fail=1")
            return 2
    import vanilla_index as vi
    with open(a.contract, encoding="utf-8") as f:
        contract = json.load(f)
    ctx = Ctx(contract, vi.load(a.index), a.esm, {"P1": not a.no_p1, "P2": not a.no_p2})
    results = []

    def chk(ok, name, detail=""):
        results.append((bool(ok), name, detail))
        if not a.quiet or not ok:
            print("%s %s%s" % ("PASS" if ok else "FAIL", name, (": " + detail) if detail else ""))

    try:
        load_templates(ctx)
        main_p, main_recs, fails = build_plugin(ctx, contract["plugin"]["file"],
                                                {"author": contract["plugin"].get("author"),
                                                 "description": contract["plugin"].get("description")})
        for f in fails:
            chk(False, "build " + contract["plugin"]["file"], f)
        test_p, test_recs = None, {}
        if not a.only_main:
            test_p, test_recs, fails2 = build_plugin(ctx, contract["test_plugin"]["file"],
                                                     {"author": contract["plugin"].get("author"),
                                                      "description": "Super Peasant in-game tests (removed after "
                                                                     "each test run)"})
            for f in fails2:
                chk(False, "build " + contract["test_plugin"]["file"], f)
    except BuildError as e:
        chk(False, "build", str(e))
        print("RESULT pass=%d fail=%d" % (sum(1 for r in results if r[0]), sum(1 for r in results if not r[0])))
        return 1

    p0 = [r["edid"] for r in contract["records"] if r["priority"] == "P0"
          and (r["plugin"] == contract["plugin"]["file"] or not a.only_main)]
    missing = [e for e in p0 if e not in ctx.built]
    chk(not missing, "every P0 record built (%d)" % len(p0), ",".join(missing))
    opt = [r["edid"] for r in contract["records"] if r["priority"] != "P0"]
    chk(True, "optional records built: %s" % (",".join(e for e in opt if e in ctx.built) or "none"))

    written = {}
    if not any(not r[0] for r in results):
        targets = [(main_p, a.out_main)] + ([(test_p, a.out_test)] if test_p is not None else [])
        for plug, path in targets:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            n = ew.write_plugin(plug, path)
            back = er.Plugin.load(path)
            same = ew.plugin_bytes(back, update_hedr=False) == open(path, "rb").read()
            nrec = sum(1 for _ in back.iter_records())
            chk(same and nrec == len(main_recs if plug is main_p else test_recs),
                "wrote %s (%d bytes, %d records, re-read identical)" % (os.path.basename(path), n, nrec))
            written[os.path.basename(path)] = {"path": path, "bytes": n, "sha256": sha(path), "records": nrec}

    report = {
        "generator": "tools/esp/build_esp.py (agent B2)",
        "contract": a.contract, "esm": a.esm, "outputs": written,
        "built": sorted(ctx.built), "decisions": ctx.decisions,
        "records": {ed: {"type": ctx.forms[ed]["type"], "formid": hx(ctx.ours(ed, ctx.forms[ed]["plugin"])),
                         "plugin": ctx.forms[ed]["plugin"], "notes": ctx.notes.get(ed, [])}
                    for ed in sorted(ctx.built)},
    }
    try:
        os.makedirs(os.path.dirname(a.report), exist_ok=True)
        with open(a.report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    except OSError as e:
        print("WARN could not write report: %s" % e)
    if not a.quiet:
        for d in ctx.decisions:
            print("NOTE " + d)
    npass = sum(1 for r in results if r[0])
    nfail = len(results) - npass
    print("RESULT pass=%d fail=%d" % (npass, nfail))
    return 0 if nfail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
