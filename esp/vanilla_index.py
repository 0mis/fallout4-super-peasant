"""Build <FO4W>\\work\\vanilla_index.json: EditorID -> FormID + record type for vanilla Fallout4.esm forms.

Usage (run with -I; reads Fallout4.esm only):
  python -I vanilla_index.py [--esm <Fallout4.esm>] [--out <vanilla_index.json>] [--check]

Indexed in full (every record that has an EDID):
  the 15 types we write  NPC_ QUST PACK ARMO ARMA OTFT MGEF SPEL ALCH MESG GLOB FLST PERK EXPL SNDR
  referenced types       RACE KYWD FACT AVIF IDLE STAT AACT SNCT SOPM (sound category / output model of our SNDRs)
  helpful extras         WRLD CELL LCTN CSTY WEAP LVLN CLAS EQUP
  marker references      every REFR with an EDID whose base is a marker STAT (EDID contains "Marker", or one of the
                         engine markers XMarker/XMarkerHeading/MapMarker) or that carries map-marker data (XMRK),
                         with its base, cell, worldspace, position and rotation.

JSON layout
  meta        source path/size/mtime, counts per type, generator
  forms       {EDID: {"formid": "0x00013746", "type": "RACE"[, "flags": "0x..."]}}  (first record wins on a clash)
  duplicates  {EDID: [{"formid", "type"}, ...]} for EDIDs used by more than one record (all entries listed)
  no_edid     {type: [formid, ...]} records of the indexed types that have no EDID (exterior CELLs left out)
  refs        {EDID: {"formid", "type": "REFR", "base", "base_edid", "cell", "cell_edid", "interior",
                       "world", "world_edid", "pos": [x,y,z], "rot": [x,y,z] (radians), "map_marker": bool}}
  engine      forms the engine defines without a record in Fallout4.esm (PlayerRef 0x14)
  contract_vanilla  {"contract": path, "verified": {EDID: {"formid", "type"}}, "all_ok": bool}: every vanilla
              FormID that src/contract.json names (vanilla table, record templates, script property values),
              re-checked against Fallout4.esm (CONTRACT.md build step 4)
  named       the vanilla forms PLAN 2.1 / 5.3 name, resolved: HumanRace, PlayerRef, Player, PlayerFaction,
              ActionRightAttack, ActionLeftAttack, Health, actor_type_keywords {EDID: formid}, raider_npc,
              raider_npc_candidates, outdoor_marker (+ candidates), QASmoke cell, Commonwealth, Vault 111 locations.

Lookup helper for other tools:  import vanilla_index; vanilla_index.load(path).formid("HumanRace") -> 0x13746
"""
import argparse
import datetime
import json
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import esm_reader as er  # noqa: E402

DEFAULT_ESM = r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm"
DEFAULT_OUT = os.path.normpath(os.path.join(HERE, "..", "..", "work", "vanilla_index.json"))

WRITE_TYPES = ["NPC_", "QUST", "PACK", "ARMO", "ARMA", "OTFT", "MGEF", "SPEL", "ALCH", "MESG", "GLOB", "FLST",
               "PERK", "EXPL", "SNDR"]
REF_TYPES = ["RACE", "KYWD", "FACT", "AVIF", "IDLE", "STAT", "AACT", "SNCT", "SOPM", "VTYP"]  # VTYP: C247 E1 voice
EXTRA_TYPES = ["WRLD", "CELL", "LCTN", "CSTY", "WEAP", "LVLN", "CLAS", "EQUP"]
INDEX_TYPES = WRITE_TYPES + REF_TYPES + EXTRA_TYPES
ENGINE_MARKERS = {0x10: "MapMarker", 0x34: "XMarkerHeading", 0x3B: "XMarker"}

# Forms the engine hard-codes (no record in Fallout4.esm; verified by the walk below).
ENGINE_FORMS = {"PlayerRef": {"formid": 0x14, "type": "ACHR", "base": 0x7,
                              "note": "engine-defined player reference; Fallout4.esm has no record 0x14"}}

# Same picks as CONTRACT.md section 10 (LvlRaiderMelee, SanctuaryMapMarker); the others are fallbacks.
PREFERRED_RAIDERS = ["LvlRaiderMelee", "LvlRaiderNoLegendary", "EncRaider02", "EncRaider01Template"]
PREFERRED_OUTDOOR = ["SanctuaryMapMarker", "RedRocketTruckStopMapMarker", "ConcordMuseumMapMarker"]
DEFAULT_CONTRACT = os.path.normpath(os.path.join(HERE, "..", "..", "src", "contract.json"))


def hx(v):
    return "0x%08X" % v


def first_sub(raw, sig):
    """Fast scan of an uncompressed body for the first subrecord `sig` (bytes) without building objects."""
    i, n = 0, len(raw)
    while i + 6 <= n:
        s = raw[i:i + 4]
        size = struct.unpack_from("<H", raw, i + 4)[0]
        if s == b"XXXX":
            big = struct.unpack_from("<I", raw, i + 6)[0]
            i += 10
            s = raw[i:i + 4]
            if s == sig:
                return raw[i + 6:i + 6 + big]
            i += 6 + big
            continue
        if s == sig:
            return raw[i + 6:i + 6 + size]
        i += 6 + size
    return None


def build(esm):
    t0 = time.time()
    want = set(INDEX_TYPES)
    forms, dups, counts, all_counts, no_edid = {}, {}, {}, {}, {}
    seen_fids = set()
    cell_edid, world_edid, cell_interior = {}, {}, {}
    stat_edid = {}
    raw_refs = []  # (rec, cell_fid, world_fid)
    fid14 = None
    for rec, stack in er.iter_records(esm, with_path=True):
        sig = rec.sig
        all_counts[sig] = all_counts.get(sig, 0) + 1
        if rec.formid == 0x14:
            fid14 = sig
        if sig in want:
            body = rec.data_bytes()
            if sig == "CELL":
                data = first_sub(body, b"DATA")
                cell_interior[rec.formid] = bool(data and data[0] & 1)
            ed = first_sub(body, b"EDID")
            if ed is None:
                if sig != "CELL":  # exterior cells have no EDID by design (38k of them)
                    no_edid.setdefault(sig, []).append(hx(rec.formid))
                continue
            edid = ed.split(b"\0", 1)[0].decode("cp1252", errors="replace")
            counts[sig] = counts.get(sig, 0) + 1
            entry = {"formid": hx(rec.formid), "type": sig}
            if rec.flags:
                entry["flags"] = hx(rec.flags)
            key = (rec.formid, sig)
            if edid in forms:
                old = forms[edid]
                if (int(old["formid"], 16), old["type"]) != key:
                    dl = dups.setdefault(edid, [{"formid": old["formid"], "type": old["type"]}])
                    dl.append({"formid": entry["formid"], "type": sig})
            else:
                forms[edid] = entry
            seen_fids.add(rec.formid)
            if sig == "CELL":
                cell_edid[rec.formid] = edid
            elif sig == "WRLD":
                world_edid[rec.formid] = edid
            elif sig == "STAT":
                stat_edid[rec.formid] = edid
        elif sig == "REFR":
            raw = rec.stored_bytes()
            if rec.is_compressed:
                raw = rec.data_bytes()
            if raw[:4] != b"EDID":
                continue
            cell_fid = world_fid = None
            for g in stack:
                if g.gtype in (6, 8, 9):
                    cell_fid = g.label_formid
                elif g.gtype == 1:
                    world_fid = g.label_formid
            raw_refs.append((rec, raw, cell_fid, world_fid))
    refs = {}
    for rec, raw, cell_fid, world_fid in raw_refs:
        base = first_sub(raw, b"NAME")
        if base is None:
            continue
        base = struct.unpack("<I", base[:4])[0]
        bed = stat_edid.get(base)
        xmrk = first_sub(raw, b"XMRK") is not None
        is_marker = base in ENGINE_MARKERS or (bed is not None and "marker" in bed.lower())
        if not (is_marker or xmrk):
            continue
        edid = first_sub(raw, b"EDID").split(b"\0", 1)[0].decode("cp1252", errors="replace")
        d = first_sub(raw, b"DATA")
        pos = rot = None
        if d is not None and len(d) >= 24:
            v = struct.unpack_from("<6f", d, 0)
            pos = [round(x, 3) for x in v[:3]]
            rot = [round(x, 5) for x in v[3:]]
        refs[edid] = {
            "formid": hx(rec.formid), "type": "REFR", "base": hx(base),
            "base_edid": ENGINE_MARKERS.get(base, bed), "cell": hx(cell_fid) if cell_fid is not None else None,
            "cell_edid": cell_edid.get(cell_fid), "interior": cell_interior.get(cell_fid),
            "world": hx(world_fid) if world_fid is not None else None, "world_edid": world_edid.get(world_fid),
            "pos": pos, "rot": rot, "map_marker": xmrk,
        }
    return {
        "forms": forms, "dups": dups, "counts": counts, "all_counts": all_counts, "refs": refs, "no_edid": no_edid,
        "fid14": fid14, "seconds": round(time.time() - t0, 1),
    }


def pick_named(forms, refs, fid14):
    def f(ed, typ=None):
        e = forms.get(ed)
        if e is None or (typ and e["type"] != typ):
            return None
        return {"edid": ed, "formid": e["formid"], "type": e["type"]}

    named = {
        "HumanRace": f("HumanRace", "RACE"),
        "Player": f("Player", "NPC_"),
        "PlayerRef": {"edid": "PlayerRef", "formid": hx(0x14), "type": "ACHR", "in_esm": fid14 is not None,
                      "base": hx(0x7)},
        "PlayerFaction": f("PlayerFaction", "FACT"),
        "ActionRightAttack": f("ActionRightAttack", "AACT"),
        "ActionLeftAttack": f("ActionLeftAttack", "AACT"),
        "Health": f("Health", "AVIF"),
        "QASmoke": f("QASmoke", "CELL"),
        "Commonwealth": f("Commonwealth", "WRLD"),
        "SanctuaryHillsWorld": f("SanctuaryHillsWorld", "WRLD"),
        "Vault111Location": f("Vault111Location", "LCTN"),
        "PrewarVault111Location": f("PrewarVault111Location", "LCTN"),
        "XMarker": f("XMarker", "STAT"),
        "XMarkerHeading": f("XMarkerHeading", "STAT"),
    }
    named["actor_type_keywords"] = {ed: e["formid"] for ed, e in sorted(forms.items())
                                    if e["type"] == "KYWD" and ed.startswith("ActorType")}
    raiders = [ed for ed, e in forms.items() if e["type"] == "NPC_"
               and (ed.lower().startswith("encraider") or ed.lower().startswith("lvlraider"))
               and "dog" not in ed.lower() and "face" not in ed.lower()]
    raiders.sort()
    named["raider_npc_candidates"] = {ed: forms[ed]["formid"] for ed in raiders}
    pick = next((ed for ed in PREFERRED_RAIDERS if ed in forms and forms[ed]["type"] == "NPC_"), None)
    if pick is None and raiders:
        pick = raiders[0]
    named["raider_npc"] = f(pick, "NPC_") if pick else None
    outdoor = {ed: r for ed, r in refs.items() if r["world_edid"] == "Commonwealth" and r["map_marker"]}
    named["outdoor_marker_candidates"] = {ed: {"formid": r["formid"], "pos": r["pos"]}
                                          for ed, r in sorted(outdoor.items())}
    pick = next((ed for ed in PREFERRED_OUTDOOR if ed in outdoor), None)
    if pick is None:
        rr = sorted(ed for ed in outdoor if "redrocket" in ed.lower() or "sanctuary" in ed.lower())
        pick = rr[0] if rr else (sorted(outdoor)[0] if outdoor else None)
    named["outdoor_marker"] = dict(refs[pick], edid=pick) if pick else None
    return named


class Index(object):
    """Loaded vanilla_index.json with small helpers."""

    def __init__(self, data):
        self.data = data
        self.forms = data["forms"]
        self.refs = data.get("refs", {})

    def entry(self, edid):
        e = self.forms.get(edid) or self.refs.get(edid)
        if e is None and edid in ENGINE_FORMS:
            return {"formid": hx(ENGINE_FORMS[edid]["formid"]), "type": ENGINE_FORMS[edid]["type"]}
        return e

    def formid(self, edid, typ=None):
        e = self.entry(edid)
        if e is None:
            raise KeyError("EditorID %r is not in the vanilla index" % edid)
        if typ is not None and e["type"] != typ:
            raise KeyError("EditorID %r is a %s, not a %s" % (edid, e["type"], typ))
        return int(e["formid"], 16)

    def type_of(self, formid):
        fid = hx(formid)
        for ed, e in self.forms.items():
            if e["formid"] == fid:
                return e["type"]
        for ed, e in self.refs.items():
            if e["formid"] == fid:
                return e["type"]
        for ed, e in ENGINE_FORMS.items():
            if e["formid"] == formid:
                return e["type"]
        return None


def contract_vanilla_refs(contract):
    """Every vanilla form contract.json names: the 'vanilla' table plus every {"kind": "vanilla", ...} value
    (record templates, script property values). Returns [(where, edid, formid_int, type)]."""
    out = []
    for ed, v in sorted(contract.get("vanilla", {}).items()):
        out.append(("vanilla/" + ed, ed, int(v["formid"], 16), v.get("type")))

    def walk(x, path):
        if isinstance(x, dict):
            if x.get("kind") == "vanilla" and "formid" in x:
                out.append(("/".join(path), x.get("edid"), int(x["formid"], 16), x.get("type")))
            for k, v in x.items():
                walk(v, path + [str(k)])
        elif isinstance(x, list):
            for i, v in enumerate(x):
                walk(v, path + [str(i)])

    walk({k: v for k, v in contract.items() if k != "vanilla"}, [])
    return out


def contract_check(idx, contract_path):
    """Re-check every vanilla FormID in contract.json against the index. Returns [(ok, name, detail)] and the
    verified table {edid: {"formid", "type"}}."""
    with open(contract_path, encoding="utf-8") as f:
        contract = json.load(f)
    rows = contract_vanilla_refs(contract)
    res, table = [], {}
    for where, ed, fidv, typ in rows:
        e = idx.entry(ed) if ed else None
        if e is None:
            res.append((False, "contract %s" % where, "EditorID %r not in Fallout4.esm index" % ed))
            continue
        ok = int(e["formid"], 16) == fidv and (typ is None or e["type"] == typ)
        detail = "contract %s %s, esm %s %s" % (hx(fidv), typ, e["formid"], e["type"])
        if ok and fidv >> 24 != 0:
            ok, detail = False, "master index is not 00 (Fallout4.esm): " + hx(fidv)
        res.append((ok, "contract %s = %s" % (where, ed), detail))
        if ok:
            table[ed] = {"formid": e["formid"], "type": e["type"]}
    if not rows:
        res.append((False, "contract has vanilla forms", contract_path))
    return res, table


def load(path=DEFAULT_OUT):
    with open(path, encoding="utf-8") as f:
        return Index(json.load(f))


def self_check(idx):
    """Return [(ok, name, detail)] sanity checks on a built index."""
    fx = idx.forms
    out = []

    def chk(ok, name, detail=""):
        out.append((bool(ok), name, detail))

    expect = {"HumanRace": ("RACE", 0x13746), "Player": ("NPC_", 0x7), "PlayerFaction": ("FACT", 0x1C21C),
              "ActionRightAttack": ("AACT", 0x13005), "ActionLeftAttack": ("AACT", 0x13004),
              "Health": ("AVIF", 0x2D4), "ActorTypeHuman": ("KYWD", 0x2CB72), "ActorTypeNPC": ("KYWD", 0x13794),
              "QASmoke": ("CELL", 0x1F7A9), "Commonwealth": ("WRLD", 0x3C), "GameHour": ("GLOB", 0x38)}
    for ed, (t, v) in expect.items():
        e = fx.get(ed)
        chk(e is not None and e["type"] == t and int(e["formid"], 16) == v, "known form %s" % ed, str(e))
    counts = idx.data["meta"]["counts"]
    for t in INDEX_TYPES:
        chk(counts.get(t, 0) > 0, "type indexed %s" % t, str(counts.get(t)))
    for t in WRITE_TYPES:  # every record of the types we write has an EDID in vanilla? report coverage
        tot = idx.data["meta"]["all_counts"].get(t, 0)
        chk(counts.get(t, 0) >= tot * 0.95, "EDID coverage %s" % t, "%s of %s" % (counts.get(t), tot))
    n = idx.data["named"]
    for k in ("HumanRace", "PlayerFaction", "ActionRightAttack", "ActionLeftAttack", "Health", "raider_npc",
              "outdoor_marker", "QASmoke", "Commonwealth"):
        chk(n.get(k) is not None, "named %s" % k)
    chk(len(n["actor_type_keywords"]) >= 20, "ActorType keywords", str(len(n["actor_type_keywords"])))
    om = n.get("outdoor_marker") or {}
    chk(om.get("world_edid") == "Commonwealth" and om.get("interior") is False and om.get("pos"),
        "outdoor marker is an exterior Commonwealth ref with a position", str(om.get("edid")))
    chk(n["PlayerRef"]["formid"] == "0x00000014" and not n["PlayerRef"]["in_esm"], "PlayerRef engine-defined")
    rn = n.get("raider_npc") or {}
    chk(rn.get("type") == "NPC_", "raider is an NPC_", str(rn))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--esm", default=DEFAULT_ESM)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--check", action="store_true", help="only run the self-check on an existing index")
    ap.add_argument("--contract", default=DEFAULT_CONTRACT,
                    help="contract.json whose vanilla FormIDs are re-checked (skipped if the file is missing)")
    a = ap.parse_args()
    if not a.check:
        r = build(a.esm)
        st = os.stat(a.esm)
        data = {
            "meta": {
                "source": a.esm, "size": st.st_size,
                "mtime": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds"),
                "generated": datetime.datetime.now().isoformat(timespec="seconds"),
                "generator": "tools/esp/vanilla_index.py", "types": INDEX_TYPES,
                "counts": r["counts"], "all_counts": {t: r["all_counts"].get(t, 0) for t in INDEX_TYPES + ["REFR"]},
                "refs": len(r["refs"]), "seconds": r["seconds"],
                "formid_format": "hex string 0xXXXXXXXX, load-order index 00 = Fallout4.esm",
            },
            "engine": {k: dict(v, formid=hx(v["formid"]), base=hx(v["base"])) for k, v in ENGINE_FORMS.items()},
            "named": pick_named(r["forms"], r["refs"], r["fid14"]),
            "forms": r["forms"],
            "duplicates": r["dups"],
            "no_edid": r["no_edid"],
            "refs": r["refs"],
        }
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        tmp = a.out + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=0, sort_keys=False)
        os.replace(tmp, a.out)
        print("wrote %s: %d forms, %d duplicate EDIDs, %d marker refs in %.1f s"
              % (a.out, len(r["forms"]), len(r["dups"]), len(r["refs"]), r["seconds"]))
        print("counts:", json.dumps(r["counts"], sort_keys=True))
    idx = load(a.out)
    res = self_check(idx)
    if a.contract and os.path.exists(a.contract):
        cres, table = contract_check(idx, a.contract)
        res += cres
        print("contract: %d vanilla references re-checked (%d distinct forms), %d ok"
              % (len(cres), len(table), sum(1 for ok, _, _ in cres if ok)))
        if not a.check:
            # record the verified contract table inside the index for build_esp.py
            idx.data["contract_vanilla"] = {"contract": a.contract, "verified": table,
                                            "all_ok": all(ok for ok, _, _ in cres)}
            tmp = a.out + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(idx.data, f, indent=0, sort_keys=False)
            os.replace(tmp, a.out)
    else:
        print("contract: %s not found, contract re-check skipped" % a.contract)
    npass = sum(1 for ok, _, _ in res if ok)
    for ok, name, detail in res:
        if not ok:
            print("FAIL", name, detail)
    n = idx.data["named"]
    for k in ("HumanRace", "PlayerRef", "PlayerFaction", "ActionRightAttack", "ActionLeftAttack", "Health",
              "raider_npc", "QASmoke"):
        print("  %-18s %s" % (k, n.get(k)))
    om = n.get("outdoor_marker")
    if om:
        print("  %-18s %s %s %s pos=%s" % ("outdoor_marker", om["edid"], om["formid"], om["world_edid"], om["pos"]))
    print("RESULT pass=%d fail=%d" % (npass, len(res) - npass))
    return 0 if npass == len(res) else 1


if __name__ == "__main__":
    sys.exit(main())
