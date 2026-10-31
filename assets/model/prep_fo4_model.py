# Super Peasant -> Fallout 4: mesh re-rig PREPARATION (no FO4, no PyNifly needed).
#
#   uv run --offline --python 3.11 --with numpy --with matplotlib python -I fo4\model\prep_fo4_model.py [units_per_metre] [size]
#   defaults: units_per_metre 64.2  (TABS size-1 head top 1.99386 m == 128 u, the Creation Engine "6 ft human"), size 1.0
#
# Reads only native\model_src\ (superpeasant_model.json, skeleton.json, meshes\*.json). Writes only fo4\model\:
#   bone_map_fo4.json / bone_map_fo4.csv    18 M_ bones + 11 rigidbodies + 2 fists -> FO4 bones (option 1 smooth, option 2 rigid),
#                                           TABS joints in FO4 space, the fit chain used by blender\fit_export_fo4.py
#   superpeasant_fo4_prep.json              4 shapes (smooth_lit, smooth_glow, rigid_lit, rigid_glow): FO4-space vertices,
#                                           normals, palette UVs, TABS UVs, triangles (winding fixed), weights
#   superpeasant_fo4_rest_red.obj/.mtl      the smooth shapes for eyeballing (FO4 axes: +X right, +Y forward, +Z up)
#   Data\Textures\SuperPeasant\*.dds/.png   palette diffuse (red + blue team), flat normal map, black specular
#   preview_fo4_*.png                       front/side previews (palette colours; option-2 bone colours)
#   prep_report.json                        counts and checks
#
# Axes.  Unity: left-handed, +X = unit's right, +Y up, +Z forward, metres.
#        FO4 / NIF actor space: right-handed, +X = actor's right, +Y forward, +Z up, game units
#        (PyNifly wiki: without blender_xf "nif coordinates are used directly. Most meshes will have their back to you",
#         i.e. actors face +Y; PyNifly test_fo4.py: LLeg_Foot at x -19.54, i.e. left = -X).
#   fo4 = P @ unity * units_per_metre * size,  P = [[1,0,0],[0,0,1],[0,1,0]]  (x, y, z) -> (x, z, y).  det P = -1, which is
#   exactly the left->right-handed change, so the shape is NOT mirrored (his left arm stays on -X = his left) but every
#   triangle's winding must be reversed (a,b,c) -> (c,b,a); a mirrored part (right bracer, det < 0) is reversed twice.
import os, sys, json, math, struct, zlib, csv
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
WS = os.path.normpath(os.path.join(HERE, "..", ".."))
MS = os.path.join(WS, "native", "model_src")
OUT = HERE
TEX = os.path.join(OUT, "Data", "Textures", "SuperPeasant")
os.makedirs(TEX, exist_ok=True)

UPM = float(sys.argv[1]) if len(sys.argv) > 1 else 64.2
SIZE = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
K = UPM * SIZE
P = np.array([[1.0, 0, 0], [0, 0, 1.0], [0, 1.0, 0]])

man = json.load(open(os.path.join(MS, "superpeasant_model.json")))
skel = json.load(open(os.path.join(MS, "skeleton.json")))
MATS = man["materials"]
SK = {b["name"]: b for b in skel["unit_armature"]}
WU = {n: np.array(b["world_matrix"]) for n, b in SK.items()}
_mj = {}
def mj(key):
    if key not in _mj: _mj[key] = json.load(open(os.path.join(MS, man["meshes"][key]["json"])))
    return _mj[key]
def fo4(p): return P @ np.asarray(p, float) * K
def r6(v): return [round(float(x), 6) for x in v]

# ---------------------------------------------------------------- bone map (TABS -> FO4)
# FO4 bone names: PyNifly io_scene_nifly/pyn/niflytools.py fo4Bones (NIF names); hierarchy Root > COM > {Pelvis, SPINE1 > SPINE2 > Chest >
# {Neck > HEAD, LArm_Collarbone > LArm_UpperArm > LArm_ForeArm1 > ForeArm2 > ForeArm3 > LArm_Hand}}, Pelvis > LLeg_Thigh > LLeg_Calf > LLeg_Foot > LLeg_Toe1.
SIDE = {"Left": "L", "Right": "R"}
def fo4_smooth(m):
    """Option 1 (smooth skin on the FO4 human skeleton): which FO4 bone a TABS M_ weight goes to (before the fit-stage spine split)."""
    t = {"M_Hip": "Pelvis", "M_Torso": "SPINE2+Chest (split by height in the fit stage)", "M_Neck": "Neck", "M_Head": "HEAD"}
    if m in t: return t[m]
    part, side = m[2:].rsplit("_", 1); s = SIDE[side]
    return {"Arm": s + "Arm_UpperArm", "Elbow": s + "Arm_ForeArm1", "Wrist": s + "Arm_Hand", "Hand": s + "Arm_Hand",
            "Leg": s + "Leg_Thigh", "Knee": s + "Leg_Calf", "Foot": s + "Leg_Foot"}[part]
RB_GROUP = {"Hip": "M_Hip", "Torso": "M_Torso", "Head": "M_Head", "Arm_Left": "M_Arm_Left", "Elbow_Left": "M_Elbow_Left",
            "Arm_Right": "M_Arm_Right", "Elbow_Right": "M_Elbow_Right", "Leg_Left": "M_Leg_Left", "Knee_Left": "M_Knee_Left",
            "Leg_Right": "M_Leg_Right", "Knee_Right": "M_Knee_Right"}
RB_FO4 = {"Hip": "Pelvis", "Torso": "Chest", "Head": "HEAD", "Arm_Left": "LArm_UpperArm", "Elbow_Left": "LArm_ForeArm1",
          "Arm_Right": "RArm_UpperArm", "Elbow_Right": "RArm_ForeArm1", "Leg_Left": "LLeg_Thigh", "Knee_Left": "LLeg_Calf",
          "Leg_Right": "RLeg_Thigh", "Knee_Right": "RLeg_Calf"}
# final vertex-group rename used by the fit stage (TABS group -> FO4 bone); M_Torso is split, see fit_export_fo4.py
GROUP_TO_FO4_SMOOTH = {"M_Hip": "Pelvis", "M_Torso": "@spine_split", "M_Neck": "Neck", "M_Head": "HEAD"}
GROUP_TO_FO4_RIGID = {g: RB_FO4[rb] for rb, g in RB_GROUP.items()}
for side, s in SIDE.items():
    GROUP_TO_FO4_SMOOTH.update({"M_Arm_" + side: s + "Arm_UpperArm", "M_Elbow_" + side: s + "Arm_ForeArm1", "M_Wrist_" + side: s + "Arm_Hand",
                                "M_Hand_" + side: s + "Arm_Hand", "M_Leg_" + side: s + "Leg_Thigh", "M_Knee_" + side: s + "Leg_Calf",
                                "M_Foot_" + side: s + "Leg_Foot"})
# fit chain: (TABS bone, mode, FO4 joint at the bone head, FO4 joint giving the direction, stretch along the bone)
#   align  = move the TABS joint onto the FO4 joint, swing the TABS bone direction onto the FO4 one (no twist), optional stretch
#   follow = keep the rest offset to the TABS parent (rigid child)
#   ground = put the joint above the FO4 joint at the TABS height x scale (feet stay on the floor), keep the TABS rotation
FIT_CHAIN = [("M_Hip", "align", "Pelvis", "SPINE2", True), ("M_Torso", "align", "SPINE2", "Neck", True),
             ("M_Neck", "align", "Neck", "HEAD", False), ("M_Head", "follow", None, None, False)]
for side, s in SIDE.items():
    FIT_CHAIN += [("M_Arm_" + side, "align", s + "Arm_UpperArm", s + "Arm_ForeArm1", True),
                  ("M_Elbow_" + side, "align", s + "Arm_ForeArm1", s + "Arm_Hand", True),
                  ("M_Wrist_" + side, "align", s + "Arm_Hand", s + "Arm_Finger31", False),
                  ("M_Hand_" + side, "follow", None, None, False),
                  ("M_Leg_" + side, "align", s + "Leg_Thigh", s + "Leg_Calf", True),
                  ("M_Knee_" + side, "align", s + "Leg_Calf", s + "Leg_Foot@ground", True),   # aim at the grounded foot joint (below)
                  ("M_Foot_" + side, "ground", s + "Leg_Foot", None, False)]
# "@ground": the point (FO4 foot x, FO4 foot y, k * TABS foot-joint height), so the TABS soles stay on the floor and the shin
# stretches down to them; the FO4 ankle (LLeg_Foot) may sit higher than the TABS one (TABS M_Foot is 5.9 cm above the floor).
# TABS bone tails (for the Blender armature and the fit directions): the child joint, else a short extension
CHILD = {"M_Hip": "M_Torso", "M_Torso": "M_Neck", "M_Neck": "M_Head"}
for side in SIDE:
    CHILD.update({"M_Arm_" + side: "M_Elbow_" + side, "M_Elbow_" + side: "M_Wrist_" + side, "M_Wrist_" + side: "M_Hand_" + side,
                  "M_Leg_" + side: "M_Knee_" + side, "M_Knee_" + side: "M_Foot_" + side})
def tail_unity(b):
    if b in CHILD: return WU[CHILD[b]][:3, 3]
    h = WU[b][:3, 3]
    if b == "M_Head": return h + np.array([0, 0.063, 0])                         # to the body-mesh head top (1.99386 m)
    if b.startswith("M_Hand"): return h + np.array([0.08 * np.sign(h[0]), 0, 0])  # outward
    if b.startswith("M_Foot"): return h + np.array([0, 0, 0.12])                  # forward (Unity +Z)
    raise KeyError(b)

# ---------------------------------------------------------------- rest-pose geometry (same method as native/model_src/tools/make_smd.py part_geometry)
def selected_parts():
    sel = []
    for p in man["parts"]:
        if p["lod"] != 0 or p.get("rendered_in_game") is False: continue
        if p["kind"] == "rigid" and p["attach_bone"] is None: continue            # fist grip markers: never rendered
        st = p.get("eye_state")
        if st is not None and st != "Open": continue
        if st is None and not p["active"]: continue
        sel.append(p)
    return sel

def part_geometry(p):
    md = mj(p["mesh"])
    pos = np.array(md["positions"]); nrm = np.array(md["normals"])
    uv = np.array(md["uv0"]) if md["uv0"] else np.full((len(pos), 2), 0.5)
    if p["kind"] == "skinned":
        sk_ = md["skin"]; bones = sk_["bound_to_unit_bones"]
        mats = [WU[b] @ np.array(bp) for b, bp in zip(bones, sk_["bindposes_row_major"])]
        out = np.zeros_like(pos); outn = np.zeros_like(pos); wts = []
        for v in range(len(pos)):
            acc = np.zeros((4, 4))
            for bi, w in sk_["weights"][v]: acc += w * mats[bi]
            out[v] = (acc @ np.append(pos[v], 1))[:3]
            nn = np.linalg.inv(acc[:3, :3]).T @ nrm[v]; outn[v] = nn / np.linalg.norm(nn)
            ws = sorted(sk_["weights"][v], key=lambda x: -x[1]); tot = sum(w for _, w in ws)
            wts.append([(bones[bi], w / tot) for bi, w in ws])                   # FO4 keeps 4 weights per vertex: nothing dropped
        flip = False
    else:
        m = WU[p["attach_bone"]] @ np.array(p["local_matrix_to_bone"])
        out = (m @ np.hstack([pos, np.ones((len(pos), 1))]).T).T[:, :3]
        outn = (np.linalg.inv(m[:3, :3]).T @ nrm.T).T; outn /= np.linalg.norm(outn, axis=1, keepdims=True)
        wts = [[(p["attach_bone"], 1.0)]] * len(pos)
        flip = np.linalg.det(m[:3, :3]) < 0
    return out, outn, uv, wts, flip, md

def mat_key(p, slot, team="red"):
    k = p["materials"][slot]
    if p["team_color"] and p["team_color"]["material_slot"] == slot: k = p["team_color"][team]
    return k

# hand / foot split for option 1 (TABS hands and feet are rigid with the forearm / shin; FO4 animates LArm_Hand and LLeg_Foot)
WRIST_X = abs(WU["M_Wrist_Left"][0, 3])              # 0.7760 m
HAND_BLEND = (0.80, 0.86)                            # |x| m: forearm (wrist ring 0.78) -> hand (0.88+); bracer ends at 0.786
FOOT_BLEND = (0.075, 0.105)                          # y m: foot (toes reach forward below 0.09) -> shin
def smoothstep(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0); return t * t * (3 - 2 * t)
def split_hand_foot(pu, wts, kind):
    if kind != "skinned": return wts
    out = {}
    for b, w in wts:
        if b.startswith("M_Elbow_"):
            h = smoothstep(HAND_BLEND[0], HAND_BLEND[1], abs(pu[0]))
            if h > 0: out["M_Wrist_" + b.split("_")[-1]] = out.get("M_Wrist_" + b.split("_")[-1], 0) + w * h
            if h < 1: out[b] = out.get(b, 0) + w * (1 - h)
        elif b.startswith("M_Knee_"):
            f = 1 - smoothstep(FOOT_BLEND[0], FOOT_BLEND[1], pu[1])
            if f > 0: out["M_Foot_" + b.split("_")[-1]] = out.get("M_Foot_" + b.split("_")[-1], 0) + w * f
            if f < 1: out[b] = out.get(b, 0) + w * (1 - f)
        else:
            out[b] = out.get(b, 0) + w
    ws = sorted(((b, w) for b, w in out.items() if w > 1e-6), key=lambda x: -x[1])
    if len(ws) > 4:                                   # cannot happen with <=4 inputs and one split each, but keep FO4's limit explicit
        ws = ws[:4]
    tot = sum(w for _, w in ws)
    return [(b, w / tot) for b, w in ws]

# ---------------------------------------------------------------- palette (one 64x64 texture, 4x4 swatches of 16 px)
EMISSIVE = {k for k, m in MATS.items() if "_EMISSION" in (m["keywords"] or "")}
PAL_ORDER = ["sharedassets0.assets#822", "sharedassets0.assets#820", "sharedassets0.assets#180", "sharedassets0.assets#817",
             "sharedassets0.assets#476", "sharedassets0.assets#814", "sharedassets0.assets#532", "sharedassets0.assets#545",
             "sharedassets0.assets#709", "sharedassets0.assets#533"]
BLUE_SWAP = {"sharedassets0.assets#822": "sharedassets0.assets#821", "sharedassets0.assets#814": "sharedassets0.assets#813"}
GRID, SW = 4, 16
SLOT = {k: i for i, k in enumerate(PAL_ORDER)}
def swatch_uv(k):
    i = SLOT[k]; c, r = i % GRID, i // GRID
    return ((c + 0.5) / GRID, 1.0 - (r + 0.5) / GRID), ((c + 0.5) / GRID, (r + 0.5) / GRID)   # (Blender uv, DDS/NIF uv: v from the top)
def palette_rgba(team):
    img = np.zeros((GRID * SW, GRID * SW, 4), np.uint8); img[:] = (255, 0, 255, 255)        # unused swatches: magenta (UV error marker)
    for k, i in SLOT.items():
        kk = BLUE_SWAP.get(k, k) if team == "blue" else k
        c, r = i % GRID, i // GRID
        img[r * SW:(r + 1) * SW, c * SW:(c + 1) * SW, :3] = MATS[kk]["_Color_srgb8"]
    return img

def write_dds_bgra(path, rgba, mips):
    levels = [rgba]
    for _ in range(mips - 1):
        a = levels[-1].astype(np.float32)
        levels.append(((a[0::2, 0::2] + a[1::2, 0::2] + a[0::2, 1::2] + a[1::2, 1::2]) / 4 + 0.5).astype(np.uint8))
    h, w = rgba.shape[:2]
    hdr = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000, h, w, w * 4, 0, len(levels))
    hdr += b"\0" * 44
    hdr += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)   # DDPF_RGB|ALPHAPIXELS, B8G8R8A8
    hdr += struct.pack("<IIIII", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    assert len(hdr) == 128
    with open(path, "wb") as fh:
        fh.write(hdr)
        for lv in levels: fh.write(lv[:, :, [2, 1, 0, 3]].tobytes())
def write_png(path, rgba):
    h, w = rgba.shape[:2]
    raw = b"".join(b"\0" + rgba[y].tobytes() for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

for team in ("red", "blue"):
    img = palette_rgba(team)
    write_dds_bgra(os.path.join(TEX, "superpeasant_%s_d.dds" % team), img, 3)     # 64, 32, 16: every swatch stays solid (>= 4 px)
    write_png(os.path.join(TEX, "superpeasant_%s_d.png" % team), img)
flat_n = np.zeros((16, 16, 4), np.uint8); flat_n[:] = (128, 128, 255, 255)
write_dds_bgra(os.path.join(TEX, "superpeasant_n.dds"), flat_n, 1); write_png(os.path.join(TEX, "superpeasant_n.png"), flat_n)
black_s = np.zeros((16, 16, 4), np.uint8); black_s[:, :, 3] = 255
write_dds_bgra(os.path.join(TEX, "superpeasant_s.dds"), black_s, 1); write_png(os.path.join(TEX, "superpeasant_s.png"), black_s)

# ---------------------------------------------------------------- build the four shapes
SEGMENT_OWNER = lambda bone: SK[bone]["moves_with_rigidbody"]
shapes = {n: {"positions": [], "normals": [], "uv_palette": [], "uv_palette_nif": [], "uv_tabs": [], "weights": [], "weights_tabs": [],
              "vertex_part": [], "tris": [], "tri_material": [], "tri_segment": [], "_idx": {}} for n in ("smooth_lit", "smooth_glow", "rigid_lit", "rigid_glow")}
stats = {"parts": {}, "tri_total": 0, "seg_tris": {}, "winding_agree": 0, "winding_total": 0}
for p in selected_parts():
    out, outn, uv, wts, flip, md = part_geometry(p)
    wsplit = [split_hand_foot(out[v], wts[v], p["kind"]) for v in range(len(out))]
    owner_v = [SEGMENT_OWNER(max(wts[v], key=lambda x: x[1])[0]) if p["kind"] == "skinned" else SEGMENT_OWNER(p["attach_bone"]) for v in range(len(out))]
    ntri = 0
    for si, sm in enumerate(md["submeshes"]):
        mk = mat_key(p, si); glow = mk in EMISSIVE
        for t in sm["triangles"]:
            owners = [owner_v[v] for v in t]
            seg = max(set(owners), key=lambda r: (owners.count(r), r == owners[0]))          # spec_model.md 8.1 rule (majority, tie -> vertex 0)
            stats["seg_tris"][seg] = stats["seg_tris"].get(seg, 0) + 1
            order = (t[2], t[1], t[0]) if not flip else (t[0], t[1], t[2])
            for variant in ("smooth", "rigid"):
                S = shapes[variant + ("_glow" if glow else "_lit")]
                idx = []
                for v in order:
                    key = (p["part"], v, mk) if variant == "smooth" else (p["part"], v, mk, seg)
                    if key not in S["_idx"]:
                        S["_idx"][key] = len(S["positions"])
                        S["positions"].append(fo4(out[v])); S["normals"].append(P @ outn[v])
                        buv, nuv = swatch_uv(mk)
                        S["uv_palette"].append(buv); S["uv_palette_nif"].append(nuv); S["uv_tabs"].append((float(uv[v][0]), float(uv[v][1])))
                        S["weights"].append(wsplit[v] if variant == "smooth" else [(RB_GROUP[seg], 1.0)])
                        S["weights_tabs"].append(wts[v]); S["vertex_part"].append(p["part"])
                    idx.append(S["_idx"][key])
                S["tris"].append(idx); S["tri_material"].append(MATS[mk]["name"]); S["tri_segment"].append(seg)
            # winding check in FO4 space: geometric normal of the reversed triangle vs. the transformed vertex normals
            a, b, c = (fo4(out[v]) for v in order); fn = np.cross(b - a, c - a)
            vn = sum(P @ outn[v] for v in order)
            if np.linalg.norm(fn) > 1e-12:
                stats["winding_total"] += 1; stats["winding_agree"] += int(np.dot(fn, vn) > 0)
            ntri += 1
    stats["parts"][p["part"]] = ntri; stats["tri_total"] += ntri

# ---------------------------------------------------------------- joints, bone map
joints = {}
for b in SK:
    tail = tail_unity(b)
    joints[b] = {"tabs_parent": SK[b]["proposed_source_parent"], "rigidbody": SK[b]["moves_with_rigidbody"],
                 "unity_world_m": r6(WU[b][:3, 3]), "unity_rot_xyzw": r6(SK[b]["world_rot_xyzw"]),
                 "fo4_head_u": r6(fo4(WU[b][:3, 3])), "fo4_tail_u": r6(fo4(tail)),
                 "fo4_head_u_size1.15": r6(fo4(WU[b][:3, 3]) * 1.15),
                 "weighted_in_tabs": bool(SK[b]["used_by_body_mesh"]), "option1_fo4": fo4_smooth(b),
                 "option2_fo4": RB_FO4[SK[b]["moves_with_rigidbody"]]}
vcount = {}
for S in (shapes["smooth_lit"], shapes["smooth_glow"]):
    for ws in S["weights_tabs"]:
        for bn, w in ws:
            if w > 0: vcount[bn] = vcount.get(bn, 0) + 1
for b in joints: joints[b]["vertices_with_tabs_weight"] = vcount.get(b, 0)
segments = {}
for rb, info in skel["rigidbodies"].items():
    segments[rb] = {"tabs_group": RB_GROUP[rb], "fo4_bone": RB_FO4[rb], "triangles": stats["seg_tris"].get(rb, 0), "mass_kg": info.get("mass"),
                    "fo4_origin_u": r6(fo4(info["world_pos"]))}
segments["Fist_Right"] = {"tabs_group": None, "fo4_bone": "RArm_Hand (no geometry: TABS never renders the fists)", "triangles": 0}
segments["Fist_Left"] = {"tabs_group": None, "fo4_bone": "LArm_Hand (no geometry: TABS never renders the fists)", "triangles": 0}
UNUSED_FO4 = ["Root", "COM", "SPINE1 (option 1: optional share of the upper M_Hip weight, see fit stage)", "LArm_Collarbone / RArm_Collarbone",
              "*_UpperTwist1/2, *_ForeArm2/3 (twist helpers)", "*_Finger11..53", "LLeg_Toe1 / RLeg_Toe1", "every *_skin helper bone",
              "Weapon, AnimObject*, PipboyBone, face bones, cloth/CBP bones"]
bone_map = {"units_per_metre": UPM, "size": SIZE, "axis_map": "fo4 = (x, z, y)_unity * units_per_metre * size; FO4 +X right, +Y forward, +Z up",
            "winding": "reverse every triangle (det -1); mirrored parts (right bracer) twice",
            "joints": joints, "segments_option2": segments, "group_to_fo4_option1": GROUP_TO_FO4_SMOOTH, "group_to_fo4_option2": GROUP_TO_FO4_RIGID,
            "fit_chain": [{"tabs_bone": a, "mode": m, "fo4_head": h, "fo4_dir": d, "stretch": s} for a, m, h, d, s in FIT_CHAIN],
            "split": {"hand_blend_abs_x_m": HAND_BLEND, "foot_blend_y_m": FOOT_BLEND, "wrist_x_m": WRIST_X},
            "fo4_bones_left_unweighted": UNUSED_FO4,
            "scales": {"contract_64.2": "TABS size-1 head top (1.99386 m) == 128 u, FO4/CK '6 ft human' (same rule as the L4D2 36.1 u/m)",
                       "metric_69.99": "1 u = 1.428 cm (64 u per yard): real metres"}}
json.dump(bone_map, open(os.path.join(OUT, "bone_map_fo4.json"), "w"), indent=1)
with open(os.path.join(OUT, "bone_map_fo4.csv"), "w", newline="") as fh:
    w = csv.writer(fh)
    w.writerow(["tabs_bone", "tabs_parent", "rigidbody", "weighted_in_tabs", "verts", "option1_smooth_fo4", "option2_rigid_fo4",
                "fit_mode", "fit_fo4_head", "fit_fo4_dir", "stretch", "fo4_x_u", "fo4_y_u", "fo4_z_u"])
    fit = {a: (m, h, d, s) for a, m, h, d, s in FIT_CHAIN}
    for b, j in joints.items():
        m, h, d, s = fit[b]
        w.writerow([b, j["tabs_parent"], j["rigidbody"], j["weighted_in_tabs"], j["vertices_with_tabs_weight"], j["option1_fo4"], j["option2_fo4"],
                    m, h, d, s] + [round(x, 3) for x in j["fo4_head_u"]])

# ---------------------------------------------------------------- write the prep JSON + OBJ
report = {"units_per_metre": UPM, "size": SIZE, "triangles_total": stats["tri_total"], "triangles_by_part": stats["parts"],
          "triangles_by_segment": stats["seg_tris"],
          "winding_check": {"triangles": stats["winding_total"], "agree_with_vertex_normals": stats["winding_agree"],
                            "fraction": round(stats["winding_agree"] / stats["winding_total"], 5)}}
prep = {"meta": {k: bone_map[k] for k in ("units_per_metre", "size", "axis_map", "winding")},
        "palette": {"grid": GRID, "swatch_px": SW, "size_px": GRID * SW,
                    "slots": {MATS[k]["name"]: {"slot": SLOT[k], "srgb8_red_team": MATS[k]["_Color_srgb8"],
                                                "srgb8_blue_team": MATS[BLUE_SWAP.get(k, k)]["_Color_srgb8"],
                                                "uv_blender": swatch_uv(k)[0], "uv_nif": swatch_uv(k)[1], "emissive": k in EMISSIVE} for k in PAL_ORDER},
                    "textures": {"red": "Textures\\SuperPeasant\\superpeasant_red_d.dds", "blue": "Textures\\SuperPeasant\\superpeasant_blue_d.dds",
                                 "normal": "Textures\\SuperPeasant\\superpeasant_n.dds", "specular": "Textures\\SuperPeasant\\superpeasant_s.dds"}},
        "joints": {b: {"parent": j["tabs_parent"], "head": j["fo4_head_u"], "tail": j["fo4_tail_u"]} for b, j in joints.items()},
        "shapes": {}}
allp = []
for n, S in shapes.items():
    pos = np.array(S["positions"]); allp.append(pos)
    prep["shapes"][n] = {"vertex_count": len(pos), "triangle_count": len(S["tris"]),
                         "positions": [r6(v) for v in pos], "normals": [r6(v) for v in S["normals"]],
                         "uv_palette": [r6(v) for v in S["uv_palette"]], "uv_palette_nif": [r6(v) for v in S["uv_palette_nif"]],
                         "uv_tabs": [r6(v) for v in S["uv_tabs"]], "tris": S["tris"], "tri_material": S["tri_material"], "tri_segment": S["tri_segment"],
                         "weights": [[[b, round(w, 6)] for b, w in ws] for ws in S["weights"]],
                         "weights_tabs": [[[b, round(w, 6)] for b, w in ws] for ws in S["weights_tabs"]],
                         "vertex_part": S["vertex_part"]}
    wsum = [sum(w for _, w in ws) for ws in S["weights"]]
    report.setdefault("shapes", {})[n] = {"vertices": len(pos), "triangles": len(S["tris"]), "max_weights_per_vertex": max(len(ws) for ws in S["weights"]),
                                          "weight_sum_min": round(min(wsum), 6), "weight_sum_max": round(max(wsum), 6),
                                          "groups": sorted({b for ws in S["weights"] for b, _ in ws})}
sm = np.vstack([np.array(shapes["smooth_lit"]["positions"]), np.array(shapes["smooth_glow"]["positions"])])
body = np.array([v for v, part in zip(shapes["smooth_lit"]["positions"], shapes["smooth_lit"]["vertex_part"]) if part == "body"])
report["bounds_u"] = {"min": r6(sm.min(0)), "max": r6(sm.max(0)), "height_hair_tip_u": round(float(sm[:, 2].max()), 3),
                      "head_top_body_u": round(float(body[:, 2].max()), 3), "sole_u": round(float(sm[:, 2].min()), 3),
                      "at_actor_scale_1.15": {"hair_tip_u": round(float(sm[:, 2].max()) * 1.15, 2), "head_top_u": round(float(body[:, 2].max()) * 1.15, 2)}}
json.dump(prep, open(os.path.join(OUT, "superpeasant_fo4_prep.json"), "w"))

with open(os.path.join(OUT, "superpeasant_fo4_rest_red.mtl"), "w", newline="\n") as fh:
    for k in PAL_ORDER:
        c = [x / 255 for x in MATS[k]["_Color_srgb8"]]
        fh.write("newmtl %s\nKd %.4f %.4f %.4f\n%s\n" % (MATS[k]["name"].replace(" ", "_"), c[0], c[1], c[2], "Ke %.4f %.4f %.4f" % tuple(c) if k in EMISSIVE else ""))
with open(os.path.join(OUT, "superpeasant_fo4_rest_red.obj"), "w", newline="\n") as fh:
    fh.write("# Super Peasant, FO4 actor space (+X right, +Y forward, +Z up), %.3f units per TABS metre, size %.2f. Smooth shapes.\n" % (UPM, SIZE))
    fh.write("# Blender: import with Forward = Y, Up = Z.\nmtllib superpeasant_fo4_rest_red.mtl\n")
    off = 0
    for n in ("smooth_lit", "smooth_glow"):
        S = shapes[n]; fh.write("o %s\n" % n)
        for v in S["positions"]: fh.write("v %.5f %.5f %.5f\n" % tuple(v))
        for v in S["normals"]: fh.write("vn %.5f %.5f %.5f\n" % tuple(v))
        for v in S["uv_palette"]: fh.write("vt %.5f %.5f\n" % tuple(v))
        cur = None
        for t, m in zip(S["tris"], S["tri_material"]):
            if m != cur: fh.write("usemtl %s\n" % m.replace(" ", "_")); cur = m
            fh.write("f " + " ".join("%d/%d/%d" % (i + 1 + off, i + 1 + off, i + 1 + off) for i in t) + "\n")
        off += len(S["positions"])

# palette check: the swatch under every vertex UV has that vertex's material colour
img = palette_rgba("red"); bad = 0
for n, S in shapes.items():
    for t, m in zip(S["tris"], S["tri_material"]):
        k = [kk for kk in PAL_ORDER if MATS[kk]["name"] == m][0]
        for i in t:
            u, vn = S["uv_palette_nif"][i]; px = img[int(vn * GRID * SW), int(u * GRID * SW), :3]
            bad += int(list(px) != list(MATS[k]["_Color_srgb8"]))
report["palette_check_mismatches"] = bad

# ---------------------------------------------------------------- previews (matplotlib, orthographic, painter's sort)
try:
    import matplotlib; matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.collections import PolyCollection
    SEGCOL = {"Pelvis": "#d62728", "Chest": "#ff7f0e", "HEAD": "#e6c200", "LArm_UpperArm": "#2ca02c", "LArm_ForeArm1": "#17becf",
              "RArm_UpperArm": "#1f77b4", "RArm_ForeArm1": "#9467bd", "LLeg_Thigh": "#8c564b", "LLeg_Calf": "#e377c2", "RLeg_Thigh": "#7f7f7f", "RLeg_Calf": "#bcbd22"}
    def draw(ax, variant, view, colour_by):
        polys, cols, depth = [], [], []
        for n in (variant + "_lit", variant + "_glow"):
            S = shapes[n]; pos = np.array(S["positions"])
            for t, m, sg in zip(S["tris"], S["tri_material"], S["tri_segment"]):
                tri = pos[t]
                # front: camera at +Y looking at -Y, screen right = -X (his left); side: camera at +X (his right), screen right = +Y
                if view == "front": xy, d = np.stack([-tri[:, 0], tri[:, 2]], 1), tri[:, 1].mean()
                else: xy, d = tri[:, [1, 2]], tri[:, 0].mean()
                if colour_by == "material":
                    k = [kk for kk in PAL_ORDER if MATS[kk]["name"] == m][0]; c = np.array(MATS[k]["_Color_srgb8"]) / 255
                else: c = np.array(matplotlib.colors.to_rgb(SEGCOL[RB_FO4[sg]]))
                nn = np.cross(tri[1] - tri[0], tri[2] - tri[0]); nn = nn / (np.linalg.norm(nn) + 1e-12)
                light = 0.55 + 0.45 * abs(nn @ (np.array([0.3, -0.8, 0.5]) / np.linalg.norm([0.3, -0.8, 0.5])))
                polys.append(xy); cols.append(np.clip(c * light, 0, 1)); depth.append(d)
        o = np.argsort(depth)                       # far first (painter's algorithm)
        ax.add_collection(PolyCollection([polys[i] for i in o], facecolors=[cols[i] for i in o], edgecolors="none"))
        ax.set_aspect("equal"); ax.autoscale(); ax.set_title("%s %s (%s)" % (variant, view, colour_by), fontsize=9)
        ax.axhline(0, color="#888", lw=0.5)
    for variant, colour_by, fn in (("smooth", "material", "preview_fo4_smooth_palette.png"), ("rigid", "segment", "preview_fo4_rigid_bones.png")):
        fig, axs = plt.subplots(1, 2, figsize=(10, 7))
        draw(axs[0], variant, "front", colour_by); draw(axs[1], variant, "side", colour_by)
        axs[0].set_xlabel("front view: screen right = FO4 -X (his left), u"); axs[1].set_xlabel("side view from his right: screen right = FO4 +Y (forward), u")
        fig.suptitle("Super Peasant in FO4 actor space, %.1f u per TABS m, size %.2f (z up, floor = 0)" % (UPM, SIZE), fontsize=10)
        fig.tight_layout(); fig.savefig(os.path.join(OUT, fn), dpi=110); plt.close(fig)
    report["previews"] = ["preview_fo4_smooth_palette.png", "preview_fo4_rigid_bones.png"]
except Exception as e:
    report["previews_error"] = repr(e)

json.dump(report, open(os.path.join(OUT, "prep_report.json"), "w"), indent=1)
print(json.dumps({k: report[k] for k in report if k not in ("shapes",)}, indent=1))
print(json.dumps(report["shapes"], indent=1))
