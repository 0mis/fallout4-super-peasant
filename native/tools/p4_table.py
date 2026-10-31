"""P-R6-4 table (s3 fix lane 3, REVIEW s3 finding 2): does the player see his relocations - the real render camera against
the D2 eye test (Brain::PlayerSees: 60 deg half-cone, the camera guess eye - view x 250 + 40 up, sight rays, "no ray
allowed" = SEEN).

usage: python -I p4_table.py <run folder or spn.txt> [...]  [--md out.md]

New-format lines (eyeFrom= / eyeTo= / eyeKnown= / fromSpace= / view=, the fixed probe) are counted as logged. Old-format
lines (step-3 builds up to 2836C8BA: 30 deg cone, "no ray" folded into hidden, the take-off of a space-changing jump projected
into the new camera) are re-read:
  - the take-off end of a jump in another space is dropped (the TELE jump line says why=space, or the logged player eye is
    > 1500 u from the render camera = stale player facts across a load, or the jump is > 8000 u);
  - an end whose eye ray was not allowed (every jump: the probe quieted the rays first; any end whose camera ray says -1) is
    "unknown", which the D2 rule counts as SEEN;
  - the 60 deg cone is re-applied with the view taken from the nearest 1 Hz 'PROBE|cam' line (eyeGuess = eye - view x 250 +
    40 up) when that line is within 1.5 s and consistent; the eye ray of an end that is inside 60 deg but outside 30 deg was
    never cast: unknown (reported as such, not guessed).
"""
import math
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

KV = re.compile(r"(\w+)=(\S+)")
TS = re.compile(r"^\[(\d\d):(\d\d):(\d\d)\.(\d+)\]")


def vec(s):
    try:
        x = [float(v) for v in s.split(",")]
        return x if len(x) == 3 else None
    except (ValueError, AttributeError):
        return None


def sub(a, b):
    return [a[0] - b[0], a[1] - b[1], a[2] - b[2]]


def norm(a):
    return math.sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2])


def cone(eye, view, p, deg):
    d = sub(p, eye)
    L = norm(d)
    if L < 1e-3:
        return True
    return (d[0] * view[0] + d[1] * view[1] + d[2] * view[2]) / (L * max(1e-9, norm(view))) >= math.cos(math.radians(deg))


def parse(path):
    p4, cams, tele = [], [], []
    with open(path, encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            m = TS.match(ln)
            if not m:
                continue
            wall = int(m[1]) * 3600 + int(m[2]) * 60 + int(m[3]) + float("0." + m[4])
            if "SPN|R6|PROBE|P4 reloc " in ln:
                p4.append((wall, dict(KV.findall(ln.split("P4 reloc ", 1)[1]))))
            elif "SPN|R6|PROBE|cam " in ln:
                cams.append((wall, dict(KV.findall(ln.split("PROBE|cam ", 1)[1]))))
            elif "SPN|TELE|jump " in ln:
                tele.append((wall, dict(KV.findall(ln.split("TELE|jump ", 1)[1]))))
    return p4, cams, tele


def view_from_cam(wall, eye, cams):
    best = None
    for w, c in cams:
        if abs(w - wall) <= 1.5 and (best is None or abs(w - wall) < abs(best[0] - wall)):
            best = (w, c)
    if not best or not eye:
        return None
    g = vec(best[1].get("eyeGuess"))
    if not g:
        return None
    v = sub([eye[0], eye[1], eye[2] + 40.0], g)  # = view x 250
    L = norm(v)
    if not (200.0 <= L <= 300.0):
        return None  # the player moved / turned between the two lines
    return [v[0] / L, v[1] / L, v[2] / L]


def classify(wall, f, cams, tele):
    """-> dict(kind, camOn, camSeen, eye) with eye in seen-measured / seen-assumed / hidden / out / open (old rows only)"""
    kind = f.get("kind", "?")
    new = "eyeFrom" in f
    eye = vec(f.get("eye"))
    cam = vec(f.get("camPos"))
    frm, to = vec(f.get("from")), vec(f.get("to"))
    onF, onT = f.get("onFrom") == "1", f.get("onTo") == "1"
    rayF, rayT = f.get("rayFrom", "-1"), f.get("rayTo", "-1")
    if new:
        other = f.get("fromSpace") == "other"
    else:
        why = next((t.get("why") for w, t in tele if abs(w - wall) <= 0.05), None)
        stale = bool(eye and cam and norm(sub(eye, cam)) > 1500.0)
        far = bool(frm and to and norm(sub(frm, to)) > 8000.0)
        other = kind == "jump" and (why == "space" or stale or far)
    if other:
        onF, rayF = False, "-1"
    camOn = onF or onT
    camSeen = (onF and rayF == "1") or (onT and rayT == "1")
    camUnknown = not camSeen and ((onF and rayF == "-1") or (onT and rayT == "-1"))
    if new:
        ends = [f.get("eyeTo")] + ([] if other else [f.get("eyeFrom")])
        if "seen" in ends:
            e = "seen-measured"
        elif "unk" in ends:
            e = "seen-assumed"
        elif "hid" in ends:
            e = "hidden"
        else:
            e = "out"
        return dict(kind=kind, camOn=camOn, camSeen=camSeen, camUnknown=camUnknown, eye=e, fmt="new", other=other)
    # old rows: was an eye ray allowed at that moment?
    raysOff = kind == "jump" or (onF and rayF == "-1") or (onT and rayT == "-1")
    view = view_from_cam(wall, eye, cams)
    pts = [p for p in ([to] + ([] if other else [frm])) if p]
    pts = [[p[0], p[1], p[2] + 60.0] for p in pts]
    inview = None
    if view is not None and eye:
        guess = [eye[0] - view[0] * 250.0, eye[1] - view[1] * 250.0, eye[2] - view[2] * 250.0 + 40.0]
        in60 = [cone(eye, view, p, 60.0) or cone(guess, view, p, 60.0) for p in pts]
        in30 = [cone(eye, view, p, 30.0) or cone(guess, view, p, 30.0) for p in pts]
        inview = any(in60)
        neverCast = any(a and not b for a, b in zip(in60, in30))
    if f.get("seenEye") == "1":
        e = "seen-measured"  # a clear ray inside 30 deg is a clear ray inside 60 deg
    elif inview is None:
        e = "seen-assumed" if raysOff else "open"
    elif not inview:
        e = "out"
    elif raysOff:
        e = "seen-assumed"
    elif neverCast:
        e = "open"  # an end inside 60 deg but outside 30 deg: its eye ray was never cast
    else:
        e = "hidden"  # every in-view end was inside 30 deg, a ray was allowed and seenEye 0: blocked
    return dict(kind=kind, camOn=camOn, camSeen=camSeen, camUnknown=camUnknown, eye=e, fmt="old", other=other)


def main(argv):
    md = None
    if "--md" in argv:
        i = argv.index("--md")
        md = argv[i + 1]
        argv = argv[:i] + argv[i + 2:]
    rows = []
    for a in argv:
        p = Path(a)
        f = p / "spn.txt" if p.is_dir() else p
        if not f.exists():
            continue
        p4, cams, tele = parse(f)
        for wall, kv in p4:
            r = classify(wall, kv, cams, tele)
            r["run"] = f.parent.name
            r["t"] = kv.get("t", "?")
            r["hp"] = kv.get("hp", "?")
            rows.append(r)
    if not rows:
        print("no P4 reloc lines")
        return 1
    kinds = sorted(Counter(r["kind"] for r in rows).items(), key=lambda x: -x[1])
    out = []
    out.append(f"P-R6-4 recomputed: {len(rows)} relocations in {len(set(r['run'] for r in rows))} runs "
               f"({sum(r['fmt'] == 'old' for r in rows)} old-format lines, {sum(r['fmt'] == 'new' for r in rows)} new)")
    out.append("")
    out.append("| kind | n | take-off in another space (dropped) | on the real screen (>= 1 end) | camera-seen (clear ray) | camera on screen, ray unknown "
               "| D2 eye: seen (ray) | D2 eye: seen (no ray allowed = assumed) | D2 eye: hidden | D2 eye: out of view | eye not cast (old 30 deg probe) "
               "| on screen AND D2 eye hidden (measured) |")
    out.append("|---|---|---|---|---|---|---|---|---|---|---|---|")
    tot = defaultdict(int)
    for k, n in kinds + [("all", len(rows))]:
        rs = rows if k == "all" else [r for r in rows if r["kind"] == k]
        c = dict(n=len(rs), other=sum(r["other"] for r in rs), on=sum(r["camOn"] for r in rs), cs=sum(r["camSeen"] for r in rs),
                 cu=sum(r["camUnknown"] for r in rs), sm=sum(r["eye"] == "seen-measured" for r in rs), sa=sum(r["eye"] == "seen-assumed" for r in rs),
                 hid=sum(r["eye"] == "hidden" for r in rs), out=sum(r["eye"] == "out" for r in rs), op=sum(r["eye"] == "open" for r in rs),
                 bad=sum(r["camOn"] and r["eye"] == "hidden" for r in rs))
        out.append(f"| {k} | {c['n']} | {c['other']} | {c['on']} | {c['cs']} | {c['cu']} | {c['sm']} | {c['sa']} | {c['hid']} | {c['out']} | {c['op']} | {c['bad']} |")
    out.append("")
    out.append("Camera verdict x D2 eye verdict (camera seen = an end on the real screen AND a clear ray from the render camera; "
               "camera unknown = on screen, no ray allowed; camera not seen = off screen or every on-screen ray blocked):")
    out.append("")
    eyes = ["seen-measured", "seen-assumed", "hidden", "out", "open"]
    out.append("| camera \\ D2 eye | " + " | ".join(eyes) + " |")
    out.append("|---|" + "---|" * len(eyes))
    for cv, pred in (("seen", lambda r: r["camSeen"]), ("unknown", lambda r: r["camUnknown"]), ("not seen", lambda r: not r["camSeen"] and not r["camUnknown"])):
        out.append(f"| {cv} | " + " | ".join(str(sum(pred(r) and r["eye"] == e for r in rows)) for e in eyes) + " |")
    out.append("")
    out.append(f"- UNDER-report (the bad side: the player saw it, the D2 eye test says not seen): camera seen AND eye hidden / out = "
               f"{sum(r['camSeen'] and r['eye'] in ('hidden', 'out') for r in rows)}")
    for r in rows:
        if r["camSeen"] and r["eye"] in ("hidden", "out"):
            out.append(f"  - {r['run']} {r['kind']} t={r['t']} hp={r['hp']}")
    out.append(f"- over-report (the safe side: D2 would refuse a relocation the camera did not show): camera not seen AND eye seen = "
               f"{sum((not r['camSeen']) and (not r['camUnknown']) and r['eye'].startswith('seen') for r in rows)}")
    out.append(f"- verdicts that rest on 'no ray allowed' (D2 counts them seen): {sum(r['eye'] == 'seen-assumed' for r in rows)}")
    out.append(f"- old rows whose 60 deg eye ray was never cast (needs the fixed probe in game): {sum(r['eye'] == 'open' for r in rows)}")
    text = "\n".join(out)
    print(text)
    if md:
        Path(md).write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
