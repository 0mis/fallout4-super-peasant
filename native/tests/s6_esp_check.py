"""R6 step 6 (desktop, read-only): the esp carries the default object, the keyword on his NPC record and the alias flag.

uv run --no-project python -I tests/s6_esp_check.py <SuperPeasant.esp> [<installed / previous SuperPeasant.esp>] <tools/esp dir>
Checks: KYWD SP_NoPathWarpKW; DFOB NoPathFailureWarping_DO -> that keyword; SP_SuperPeasant KSIZ/KWDA has it; SP_Main alias
PeasantAlias FNAM has "Quest Object" (bit 2) and keeps its other flags; with a previous esp: every other record byte-identical.
Prints PASS / FAIL lines and RESULT pass=<n> fail=<n>; exit 1 on any failure."""
import struct
import sys

esp = sys.argv[1]
prev = sys.argv[2] if len(sys.argv) > 3 else None
sys.path.insert(0, sys.argv[-1])
import esm_reader as er  # noqa: E402

res = []


def chk(ok, name):
    res.append(ok)
    print("%s %s" % ("PASS" if ok else "FAIL", name))


def load(p):
    out = {}
    for r in er.Plugin.load(p).iter_records():
        out[r.formid] = r
    return out


R = load(esp)
by = {r.edid: r for r in R.values()}
kw = by.get("SP_NoPathWarpKW")
chk(kw is not None and kw.sig == "KYWD", "KYWD SP_NoPathWarpKW present")
do = by.get("NoPathFailureWarping_DO")
chk(do is not None and do.sig == "DFOB", "DFOB NoPathFailureWarping_DO present")
if do is not None and kw is not None:
    d = do.get("DATA")
    chk(d is not None and struct.unpack("<I", d.data)[0] == kw.formid, "DFOB DATA -> SP_NoPathWarpKW (%08X)" % kw.formid)
npc = by.get("SP_SuperPeasant")
if npc is not None and kw is not None:
    kd, ks = npc.get("KWDA"), npc.get("KSIZ")
    ids = list(struct.unpack("<%dI" % (len(kd.data) // 4), kd.data)) if kd is not None else []
    chk(kw.formid in ids, "SP_SuperPeasant keywords include SP_NoPathWarpKW")
    chk(ks is not None and struct.unpack("<I", ks.data)[0] == len(ids), "KSIZ == number of KWDA entries")
else:
    chk(False, "SP_SuperPeasant present")
q = by.get("SP_Main")
fl = None
if q is not None:
    subs = q.subrecords
    for i, s in enumerate(subs):
        if s.sig == "ALID" and bytes(s.data).rstrip(b"\0") == b"PeasantAlias":
            fl = struct.unpack("<I", subs[i + 1].data)[0] if subs[i + 1].sig == "FNAM" else None
chk(fl is not None and (fl & (1 << 2)) != 0, "PeasantAlias has Quest Object (FNAM %s)" % (hex(fl) if fl is not None else "?"))
if prev:
    P = load(prev)
    pby = {r.edid: r for r in P.values()}
    if q is not None and "SP_Main" in pby:
        pf = None
        ps = pby["SP_Main"].subrecords
        for i, s in enumerate(ps):
            if s.sig == "ALID" and bytes(s.data).rstrip(b"\0") == b"PeasantAlias":
                pf = struct.unpack("<I", ps[i + 1].data)[0]
        chk(pf is not None and fl == (pf | (1 << 2)), "PeasantAlias keeps its other flags (%s -> %s)" % (hex(pf or 0), hex(fl or 0)))
    same = 0
    changed = []
    for fid, r in P.items():
        n = R.get(fid)
        if n is None:
            changed.append("missing " + str(r.edid))
            continue
        a = [(s.sig, bytes(s.data)) for s in r.subrecords]
        b = [(s.sig, bytes(s.data)) for s in n.subrecords]
        if a == b and r.flags == n.flags:
            same += 1
        else:
            changed.append(str(r.edid))
    new = sorted(str(R[f].edid) for f in R if f not in P)
    chk(sorted(changed) == ["SP_Main", "SP_SuperPeasant"], "only SP_Main / SP_SuperPeasant changed (%s), %d identical" % (changed, same))
    chk(new == ["NoPathFailureWarping_DO", "SP_NoPathWarpKW"], "new records %s" % new)
print("RESULT pass=%d fail=%d" % (sum(res), len(res) - sum(res)))
sys.exit(0 if all(res) else 1)
