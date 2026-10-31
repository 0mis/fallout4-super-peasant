"""Byte-identical round-trip test of esm_reader + esp_writer (+ vmad) against Fallout4.esm. Read-only on the game.

Usage (run with -I):
  python -I test_roundtrip.py [--esm <Fallout4.esm>] [--full] [--report <json>]

Checks
  R1  every record of the types we write (NPC_ QUST PACK ARMO ARMA OTFT MGEF SPEL ALCH MESG GLOB FLST PERK EXPL SNDR):
      parse header + subrecords, re-serialise, compare with the file bytes (compressed records are re-deflated
      with zlib level 9 and must still match byte for byte). The decompressed subrecord stream is compared too.
  R2  every VMAD subrecord in those records: vmad.decode -> vmad.encode is byte-identical.
  R3  synthetic: XXXX large subrecord (70,000 bytes) round trip and exact header layout.
  R4  edit path: change a compressed NPC_ (EDID + a new subrecord), re-serialise, re-read, compare fields;
      template_copy() clears the compressed flag and keeps every other subrecord.
  R5  new plugin: build GLOB + FLST + a template-copied NPC_ in memory, write to bytes, re-read, re-write: identical;
      HEDR count = records + groups; masters; top-group order.
  R7  lstring subrecords listed in esp_writer.LSTRING_SIGS are 4 bytes (string IDs) everywhere in Fallout4.esm.
  R8  vmad builders (vmad_bytes/script/prop_*/quest_alias) reproduce three vanilla VMADs byte for byte
      (QUST alias script, PACK object property, ARMO object property); property_table() on a built VMAD.
  R9  every vanilla template in src/contract.json records[] loads via get_records with the right type and EDID,
      round-trips, and template_copy() output re-reads (skipped when contract.json is missing).
  R6  (--full) the WHOLE Fallout4.esm: every group and record (all 1.5 M, nested CELL/WRLD/QUST children included)
      parsed with every subrecord decoded, re-serialised, and the file rebuilt byte for byte.
Prints per-type counts and 'RESULT pass=N fail=M'. Exit code 0 only when fail=0.
"""
import argparse
import json
import mmap
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import esm_reader as er  # noqa: E402
import esp_writer as ew  # noqa: E402

try:
    import vmad  # noqa: E402
except ImportError:  # vmad.py is optional for R2
    vmad = None

DEFAULT_ESM = r"C:\Program Files (x86)\Steam\steamapps\common\Fallout 4\Data\Fallout4.esm"
TYPES = ["NPC_", "QUST", "PACK", "ARMO", "ARMA", "OTFT", "MGEF", "SPEL", "ALCH", "MESG", "GLOB", "FLST", "PERK", "EXPL",
         "SNDR"]


class Tally(object):
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.fail_lines = []

    def check(self, ok, name, detail=""):
        if ok:
            self.passed += 1
        else:
            self.failed += 1
            line = "FAIL %s %s" % (name, detail)
            self.fail_lines.append(line)
            if len(self.fail_lines) <= 40:
                print(line)
        return ok


def r1_r2_records(mm, T, report):
    """Walk the whole file (cheaply), round-trip every record of TYPES."""
    want = set(TYPES)
    per = {t: {"records": 0, "compressed": 0, "bytes_ok": 0, "stream_ok": 0, "vmad": 0, "vmad_ok": 0,
               "xxxx_subs": 0} for t in TYPES}
    n = len(mm)
    _, pos = er.read_record_at(mm, 0)
    stack = []
    vmad_fail = []
    while pos < n:
        while stack and pos >= stack[-1]:
            stack.pop()
        if mm[pos:pos + 4] == b"GRUP":
            gsize = struct.unpack_from("<I", mm, pos + 4)[0]
            label = mm[pos + 8:pos + 12].decode("latin-1")
            if not stack and label in ("CELL", "WRLD"):  # none of TYPES live there (checked by --full)
                pos += gsize
                continue
            stack.append(pos + gsize)
            pos += er.HDR_SIZE
            continue
        sig = mm[pos:pos + 4].decode("latin-1")
        dsize = struct.unpack_from("<I", mm, pos + 4)[0]
        end = pos + er.HDR_SIZE + dsize
        if sig in want:
            st = per[sig]
            st["records"] += 1
            raw = bytes(mm[pos:end])
            rec, _ = er.read_record_at(mm, pos)
            try:
                subs = rec.subrecords  # forces decode, so the writer rebuilds the body from subrecords
            except er.ESPFormatError as e:
                T.check(False, "R1 parse %s %08X" % (sig, rec.formid), str(e))
                pos = end
                continue
            st["xxxx_subs"] += sum(1 for s in subs if s.xxxx)
            if rec.is_compressed:
                st["compressed"] += 1
                stream_ok = ew.subrecords_bytes(subs) == rec.data_bytes()
            else:
                stream_ok = ew.subrecords_bytes(subs) == rec.stored_bytes()
            st["stream_ok"] += stream_ok
            out = ew.record_bytes(rec)
            ok = out == raw
            st["bytes_ok"] += ok
            T.check(ok and stream_ok, "R1 %s %08X %s" % (sig, rec.formid, rec.edid),
                    "stream_ok=%s len %d vs %d" % (stream_ok, len(out), len(raw)))
            if vmad is not None:
                for s in subs:
                    if s.sig != "VMAD":
                        continue
                    st["vmad"] += 1
                    try:
                        obj = vmad.decode(s.data, sig)
                        enc = vmad.encode(obj, sig)
                        good = enc == s.data
                        err = "" if good else "re-encoded %d vs %d bytes" % (len(enc), len(s.data))
                    except Exception as e:  # report and keep going
                        good, err = False, "%s: %s" % (type(e).__name__, e)
                    st["vmad_ok"] += good
                    if not good:
                        vmad_fail.append("%s %08X %s %s" % (sig, rec.formid, rec.edid, err))
                    T.check(good, "R2 VMAD %s %08X %s" % (sig, rec.formid, rec.edid), err)
        pos = end
    report["per_type"] = per
    report["vmad_failures"] = vmad_fail[:200]
    print("%-5s %8s %10s %9s %10s %6s %7s %5s" % ("type", "records", "compressed", "bytes_ok", "stream_ok", "vmad",
                                                 "vmad_ok", "XXXX"))
    for t in TYPES:
        s = per[t]
        print("%-5s %8d %10d %9d %10d %6d %7d %5d" % (t, s["records"], s["compressed"], s["bytes_ok"],
                                                     s["stream_ok"], s["vmad"], s["vmad_ok"], s["xxxx_subs"]))
        T.check(s["records"] > 0, "R1 type present %s" % t)
    total = sum(s["records"] for s in per.values())
    print("R1 records checked: %d" % total)
    if vmad is None:
        print("R2 skipped: vmad.py not found")


def r3_xxxx(T):
    big = bytes(range(256)) * 274 + b"x" * 16  # 70,160 bytes
    rec = ew.new_record("FLST", 0x01000800, [ew.sub("EDID", ew.zstr("SP_XXXXTest")), ew.sub("LNAM", big),
                                             ew.sub("LNAM", ew.fid(0x14))])
    b = ew.record_bytes(rec)
    i = b.find(b"XXXX\x04\x00")
    T.check(i > 0 and b[i + 4:i + 6] == b"\x04\x00" and struct.unpack_from("<I", b, i + 6)[0] == len(big)
            and b[i + 10:i + 16] == b"LNAM\x00\x00", "R3 XXXX header layout")
    back, end = er.read_record_at(b, 0)
    T.check(end == len(b), "R3 record length")
    subs = back.subrecords
    T.check([s.sig for s in subs] == ["EDID", "LNAM", "LNAM"] and subs[1].data == big and subs[1].xxxx
            and not subs[2].xxxx, "R3 XXXX decode")
    T.check(ew.record_bytes(back) == b, "R3 XXXX re-encode identical")
    # compressed + XXXX
    rec.flags |= er.FLAG_COMPRESSED
    b2 = ew.record_bytes(rec)
    back2, _ = er.read_record_at(b2, 0)
    T.check(back2.subrecords[1].data == big and ew.record_bytes(back2) == b2, "R3 XXXX inside compressed record")
    # small subrecord forced through XXXX keeps it
    s = er.Subrecord("DATA", b"abc", xxxx=True)
    T.check(er.parse_subrecords(ew.subrecords_bytes([s]))[0].xxxx, "R3 forced XXXX kept")
    # malformed input is reported, not silently accepted
    try:
        er.parse_subrecords(b"EDID\x10\x00abc")
        T.check(False, "R3 overrun detection")
    except er.ESPFormatError:
        T.check(True, "R3 overrun detection")


def first_record(mm, sig, compressed=None):
    for rec in iter_top(mm, sig):
        if compressed is None or rec.is_compressed == compressed:
            return rec
    return None


def iter_top(mm, sig):
    n = len(mm)
    _, pos = er.read_record_at(mm, 0)
    while pos < n:
        gsize = struct.unpack_from("<I", mm, pos + 4)[0]
        if mm[pos + 8:pos + 12].decode("latin-1") == sig:
            g, _ = er.read_group_at(mm, pos)
            for r in g.iter_records({sig}):
                yield r
        pos += gsize


def r4_edit(mm, T):
    npc = first_record(mm, "NPC_", compressed=True)
    T.check(npc is not None, "R4 found a compressed NPC_")
    if npc is None:
        return None
    orig_subs = [s.copy() for s in npc.subrecords]
    ed = npc.edid
    ew.set_sub(npc, "EDID", ew.zstr(ed + "_Edited"))
    ew.set_sub(npc, "ZZZZ", b"\x01\x02\x03")  # appended at the end, any unknown subrecord is carried as bytes
    b = ew.record_bytes(npc)
    back, _ = er.read_record_at(b, 0)
    T.check(back.is_compressed and back.edid == ed + "_Edited", "R4 edited compressed NPC_ re-reads")
    T.check([s.sig for s in back.subrecords] == [s.sig for s in orig_subs] + ["ZZZZ"], "R4 subrecord order kept")
    T.check(all(a == b_ for a, b_ in zip(orig_subs[1:], back.subrecords[1:])), "R4 other subrecords unchanged")
    T.check(struct.unpack_from("<I", back.stored_bytes(), 0)[0] == len(back.data_bytes()),
            "R4 decompressed-size prefix")
    tc = ew.template_copy(back, 0x01000801, "SP_TemplateTest")
    T.check(not tc.is_compressed and tc.formid == 0x01000801 and tc.edid == "SP_TemplateTest"
            and tc.vc1 == 0 and tc.vc2 == 0 and tc.version == ew.FORM_VERSION, "R4 template_copy header")
    T.check([s.sig for s in tc.subrecords] == [s.sig for s in back.subrecords]
            and all(a == b_ for a, b_ in zip(back.subrecords[1:], tc.subrecords[1:])), "R4 template_copy body")
    tb = ew.record_bytes(tc)
    T.check(struct.unpack_from("<I", tb, 8)[0] & er.FLAG_COMPRESSED == 0 and len(tb) == 24 + len(
        ew.subrecords_bytes(tc.subrecords)), "R4 template_copy written uncompressed")
    npc_fresh = first_record(mm, "NPC_", compressed=True)
    return npc_fresh


def r5_new_plugin(mm, T, npc):
    p = ew.new_plugin(["Fallout4.esm"], author="B1 test", description="round trip test")
    g1 = ew.new_record("GLOB", 0x01000800, [ew.sub("EDID", ew.zstr("SP_TestGlob")), ew.sub("FNAM", b"f"),
                                            ew.sub("FLTV", ew.f32(1.5))])
    fl = ew.new_record("FLST", 0x01000801, [ew.sub("EDID", ew.zstr("SP_TestList")), ew.sub("LNAM", ew.fid(0x13746)),
                                            ew.sub("LNAM", ew.fid(0x01000800))])
    ew.add_record(p, fl)
    ew.add_record(p, g1)
    if npc is not None:
        ew.add_record(p, ew.template_copy(npc, 0x01000802, "SP_TestNPC"))
    try:
        ew.add_record(p, ew.new_record("REFR", 0x01000803, []))
        T.check(False, "R5 add_record refuses nested types")
    except ValueError:
        T.check(True, "R5 add_record refuses nested types")
    data = ew.plugin_bytes(p, next_object_id=0x803)
    back = er.Plugin.from_bytes(data)
    T.check(back.masters == ["Fallout4.esm"], "R5 masters")
    ver, nrec, nid = back.hedr
    T.check(abs(ver - 1.0) < 1e-6 and nrec == (3 if npc is not None else 2) + 3 - (0 if npc is not None else 1)
            and nid == 0x803, "R5 HEDR", "ver=%s nrec=%d nid=%X" % (ver, nrec, nid))
    T.check([g.label_sig for g in back.groups] == (["GLOB", "NPC_", "FLST"] if npc is not None else ["GLOB", "FLST"]),
            "R5 top-group order", str([g.label_sig for g in back.groups]))
    T.check(ew.plugin_bytes(back) == data, "R5 re-read then re-write identical")
    # every record in a fresh read decodes
    for r in back.iter_records():
        r.subrecords
    T.check(back.find(edid="SP_TestList").get_all("LNAM")[1].u32() == 0x01000800, "R5 find + typed read")
    # writer refuses the game folder
    try:
        ew.write_plugin(p, os.path.join(os.path.dirname(DEFAULT_ESM), "SP_never.esp"))
        T.check(False, "R5 game-folder guard")
    except PermissionError:
        T.check(True, "R5 game-folder guard")


def r6_full(mm, T, report):
    """Whole-file rebuild, one top group at a time (memory stays small)."""
    t0 = time.time()
    n = len(mm)
    hdr, pos = er.read_record_at(mm, 0)
    hdr.subrecords
    ok_all = T.check(ew.record_bytes(hdr) == bytes(mm[0:pos]), "R6 TES4 header")
    nrec = ngrp = nsub = nxxxx = ncomp = 0
    bad_groups = []
    while pos < n:
        gsize = struct.unpack_from("<I", mm, pos + 4)[0]
        g, end = er.read_group_at(mm, pos)
        stack = [g]
        while stack:
            x = stack.pop()
            ngrp += 1
            for c in x.children:
                if isinstance(c, er.Group):
                    stack.append(c)
                else:
                    nrec += 1
                    subs = c.subrecords
                    nsub += len(subs)
                    nxxxx += sum(1 for s in subs if s.xxxx)
                    ncomp += c.is_compressed
        out = ew.group_bytes(g)
        ok = out == bytes(mm[pos:end])
        if not ok:
            bad_groups.append("%s@%d" % (g.label_sig, pos))
        ok_all &= ok
        pos = end
    T.check(ok_all and not bad_groups, "R6 whole-file rebuild", ",".join(bad_groups[:10]))
    report["full"] = {"records": nrec, "groups": ngrp, "subrecords": nsub, "xxxx_subrecords": nxxxx,
                      "compressed_records": ncomp, "bad_groups": bad_groups, "seconds": round(time.time() - t0, 1)}
    print("R6 whole file: %d records (%d compressed), %d groups, %d subrecords (%d behind XXXX) re-serialised in"
          " %.0f s; bad groups: %d" % (nrec, ncomp, ngrp, nsub, nxxxx, time.time() - t0, len(bad_groups)))
    T.check(nxxxx > 0, "R6 vanilla XXXX subrecords seen and round-tripped", str(nxxxx))


def r7_r8(mm, T, report):
    """R7 lstring sizes; R8 vmad builders reproduce vanilla VMAD bytes exactly."""
    ls = ew.LSTRING_SIGS
    sizes = {}
    by_edid = {}
    want_edids = {"DialogueGenericMerchants", "AO_Dogmeat_EscortToLinkedRef_HOSTILE", "MS09LorenzosCrown"}
    for sig in sorted(set(ls) | {"PACK", "QUST"}):
        for rec in iter_top(mm, sig):
            for s in rec.subrecords:
                if s.sig in ls.get(sig, ()):
                    k = "%s.%s" % (sig, s.sig)
                    sizes.setdefault(k, {})
                    sizes[k][len(s.data)] = sizes[k].get(len(s.data), 0) + 1
            if rec.edid in want_edids:
                by_edid[rec.edid] = rec
    report["lstring_sizes"] = sizes
    for k, c in sorted(sizes.items()):
        T.check(list(c) == [4], "R7 lstring %s always 4 bytes" % k, str(c))
    if vmad is None:
        return
    V = vmad
    cases = [
        ("DialogueGenericMerchants", "QUST",
         lambda r: V.vmad_bytes([], "QUST", aliases=[V.quest_alias(0, [V.script("JobAliasOnUnloadScript")],
                                                                   r.formid)])),
        ("AO_Dogmeat_EscortToLinkedRef_HOSTILE", "PACK",
         lambda r: V.vmad_bytes([V.script("AOPackageKeywordManager", [V.prop_object("keywordToAdd", 0x0004AC51)])],
                                "PACK")),
        ("MS09LorenzosCrown", "ARMO",
         lambda r: V.vmad_bytes([V.script("MS09LorenzosCrownScript", [V.prop_object("MS09", 0x00022A00)])],
                                "ARMO")),
    ]
    for ed, sig, build in cases:
        rec = by_edid.get(ed)
        if not T.check(rec is not None and rec.sig == sig, "R8 vanilla %s found" % ed):
            continue
        want = rec.get("VMAD").data
        got = build(rec)
        T.check(got == want, "R8 builder reproduces %s VMAD" % ed, "%d vs %d bytes" % (len(got), len(want)))
    # property_table on a built quest VMAD
    vb = V.vmad_bytes([V.script("SuperPeasant:Controller", [V.prop_float("X", 1.5), V.prop_array("L", V.T_OBJECT,
                                                                                                    [0x14, 0x7])])],
                      "QUST", aliases=[V.quest_alias(1, [V.script("SuperPeasant:PeasantAlias",
                                                                  [V.prop_bool("B", True)])], 0x01000800)])
    tbl = V.property_table(V.decode(vb, "QUST"))
    T.check(tbl == [("record", "SuperPeasant:Controller", "X", "Float"),
                    ("record", "SuperPeasant:Controller", "L", "Object[]"),
                    ("alias:1", "SuperPeasant:PeasantAlias", "B", "Bool")], "R8 property_table", str(tbl))
    T.check(V.encode(V.decode(vb, "QUST"), "QUST") == vb, "R8 built quest VMAD round trip")


def r9_templates(esm, T, contract_path):
    """R9 every vanilla template named in contract.json loads with get_records, has the expected type and EDID,
    round-trips byte for byte and survives template_copy + re-read."""
    if not contract_path or not os.path.exists(contract_path):
        print("R9 skipped: no contract.json")
        return
    with open(contract_path, encoding="utf-8") as f:
        contract = json.load(f)
    temps = {}
    for r in contract.get("records", []):
        t = r.get("template") or {}
        if t.get("kind") == "vanilla":
            temps[int(t["formid"], 16)] = (t["edid"], t["type"])
    got = er.get_records(esm, list(temps), sigs={t for _, t in temps.values()})
    for fidv, (ed, typ) in sorted(temps.items()):
        rec = got.get(fidv)
        if not T.check(rec is not None and rec.sig == typ and rec.edid == ed, "R9 template %s %08X %s" % (typ, fidv, ed),
                       str(rec)):
            continue
        raw = er.REC_HDR.pack(rec.sig.encode(), len(rec.stored_bytes()), rec.flags, rec.formid, rec.vc1, rec.vc2,
                              rec.version, rec.unknown) + rec.stored_bytes()
        rec.subrecords
        T.check(ew.record_bytes(rec) == raw, "R9 template round trip %s" % ed)
        tc = ew.template_copy(rec, 0x01000800, "SP_R9_" + ed)
        back, _ = er.read_record_at(ew.record_bytes(tc), 0)
        T.check(back.edid == "SP_R9_" + ed and back.sig_list == tc.sig_list and not back.is_compressed,
                "R9 template_copy re-read %s" % ed)
    print("R9 %d contract templates checked" % len(temps))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--esm", default=DEFAULT_ESM)
    ap.add_argument("--full", action="store_true", help="also rebuild the whole file (slow, a few minutes)")
    ap.add_argument("--report", default=None)
    ap.add_argument("--contract", default=os.path.normpath(os.path.join(HERE, "..", "..", "src", "contract.json")))
    a = ap.parse_args()
    T = Tally()
    report = {"esm": a.esm, "size": os.path.getsize(a.esm), "zlib": zlib.ZLIB_RUNTIME_VERSION}
    t0 = time.time()
    with open(a.esm, "rb") as f:
        mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
        try:
            r1_r2_records(mm, T, report)
            r3_xxxx(T)
            npc = r4_edit(mm, T)
            r5_new_plugin(mm, T, npc)
            r7_r8(mm, T, report)
            if a.full:
                r6_full(mm, T, report)
        finally:
            mm.close()
    r9_templates(a.esm, T, a.contract)
    report["pass"] = T.passed
    report["fail"] = T.failed
    report["fail_lines"] = T.fail_lines[:500]
    report["seconds"] = round(time.time() - t0, 1)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            json.dump(report, f, indent=1)
    print("time %.1f s" % (time.time() - t0))
    print("RESULT pass=%d fail=%d" % (T.passed, T.failed))
    return 0 if T.failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
