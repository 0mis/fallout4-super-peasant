# Step 7.6: One Punch Man (Saitama) costume on Super Peasant's fitted FO4 body, TABS cartoon style. Built by us from his
# fitted TABS-derived model, with new local costume/head/cape additions. See ASSET_LICENSE.md.
#
#   blender -b superpeasant_fo4_fitted.blend --python build_opm.py -- --out artifacts/model [--no-export] [--render]
#
# 1. palette: superpeasant_opm_d.dds (64x64, 4x4 flat swatches) + superpeasant_opm_s.dds (white: shiny bald head)
# 2. body (smooth + rigid variants): hair / eyes / old head removed, faces recoloured by bone + TABS material
#    (yellow suit, red gloves + boots, black belt), a black zip line down the chest, a gold belt buckle
# 3. new head (HEAD-weighted): egg-shaped bald head, plain face (eyes, pupils, small mouth, brows), ears, neck
# 4. cape: a two-layer cloth sheet hanging from the shoulders, skinned to Chest (pinned top) + 12 spare torso skin bones
#    in a 3 x 4 lattice driven at runtime by the native cloth sim (cape_model.gen.h carries the bind data)
# 5. export SuperPeasant.nif / SuperPeasant_Rigid.nif (PyNifly), shader finish, read-back, cape_bind.json, previews
import bpy, bmesh, sys, os, json, math
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def opt(n, d=None): return argv[argv.index(n) + 1] if n in argv else d
OUT = opt("--out", r"artifacts/model")
NO_EXPORT = "--no-export" in argv
RENDER = "--render" in argv
os.makedirs(OUT, exist_ok=True)
TEXD = os.path.join(OUT, "Textures", "SuperPeasant"); MESHD = os.path.join(OUT, "Meshes", "SuperPeasant")
os.makedirs(TEXD, exist_ok=True); os.makedirs(MESHD, exist_ok=True)
report = {}

# ------------------------------------------------------------------------------------------------ 1. palette
import struct, zlib
GRID, SW = 4, 16
PAL = {"suit": (246, 190, 20), "red": (200, 28, 30), "belt": (24, 24, 26), "cape": (246, 246, 246), "skin": (248, 208, 170),
       "pupil": (12, 12, 12), "eyew": (255, 255, 255), "gold": (232, 182, 38), "mouth": (110, 46, 40), "capein": (226, 226, 230),
       "zip": (36, 36, 38), "brow": (60, 42, 30)}
ORDER = list(PAL.keys())
def suv(name):
    i = ORDER.index(name); c, r = i % GRID, i // GRID
    return ((c + 0.5) / GRID, 1.0 - (r + 0.5) / GRID)
def write_dds_bgra(path, px, w, h, mips):
    import array
    levels = [px]
    cw, ch = w, h
    for _ in range(mips - 1):
        src = levels[-1]; nw, nh = cw // 2, ch // 2; dst = bytearray(nw * nh * 4)
        for y in range(nh):
            for x in range(nw):
                for k in range(4):
                    s = (src[((2*y)*cw + 2*x)*4+k] + src[((2*y)*cw + 2*x+1)*4+k] + src[((2*y+1)*cw + 2*x)*4+k] + src[((2*y+1)*cw + 2*x+1)*4+k] + 2) // 4
                    dst[(y*nw + x)*4+k] = s
        levels.append(bytes(dst)); cw, ch = nw, nh
    hdr = struct.pack("<4sIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x8 | 0x1000 | 0x20000, h, w, w * 4, 0, len(levels))
    hdr += b"\0" * 44
    hdr += struct.pack("<IIIIIIII", 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    hdr += struct.pack("<IIIII", 0x1000 | 0x400000 | 0x8, 0, 0, 0, 0)
    assert len(hdr) == 128
    with open(path, "wb") as fh:
        fh.write(hdr)
        for lv in levels:
            b = bytearray(lv)
            for i in range(0, len(b), 4): b[i], b[i+2] = b[i+2], b[i]     # RGBA -> BGRA
            fh.write(bytes(b))
def write_png(path, px, w, h):
    raw = b"".join(b"\0" + bytes(px[y*w*4:(y+1)*w*4]) for y in range(h))
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    with open(path, "wb") as fh:
        fh.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))
W = GRID * SW
pal = bytearray(W * W * 4)
for y in range(W):
    for x in range(W):
        pal[(y*W + x)*4:(y*W + x)*4+4] = bytes((255, 0, 255, 255))
for i, k in enumerate(ORDER):
    c, r = i % GRID, i // GRID
    for y in range(r*SW, (r+1)*SW):
        for x in range(c*SW, (c+1)*SW):
            pal[(y*W + x)*4:(y*W + x)*4+4] = bytes(PAL[k] + (255,))
write_dds_bgra(os.path.join(TEXD, "superpeasant_opm_d.dds"), bytes(pal), W, W, 3)
write_png(os.path.join(TEXD, "superpeasant_opm_d.png"), pal, W, W)
spec = bytes((255, 205, 0, 255)) * (16 * 16)      # R specular, G smoothness: a shiny bald head
write_dds_bgra(os.path.join(TEXD, "superpeasant_opm_s.dds"), spec, 16, 16, 1)
img = bpy.data.images.load(os.path.join(TEXD, "superpeasant_opm_d.png"), check_existing=False); img.name = "superpeasant_opm_d"
for m in bpy.data.materials:
    if m.node_tree:
        for n in m.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image and "red_d" in n.image.name: n.image = img
for n in (bpy.data.materials["SP_Palette_Lit"].node_tree.nodes if bpy.data.materials["SP_Palette_Lit"].node_tree else []):
    if n.type == 'BSDF_PRINCIPLED':
        try: n.inputs["Emission Strength"].default_value = 0.0
        except Exception: pass

ARM = bpy.data.objects["skeleton.nif:ARMATURE"]
def bone_model(name):
    b = ARM.data.bones[name]; return ARM.matrix_world @ b.matrix_local

# ------------------------------------------------------------------------------------------------ 2. body recolour
DELETE_MATS = {1, 2}      # TABS eyes, hair
def recolour(obj):
    me = obj.data
    names = {g.index: g.name for g in obj.vertex_groups}
    mat = me.attributes["tabs_material"].data
    def dom(p):
        acc = {}
        for vi in p.vertices:
            for g in me.vertices[vi].groups: acc[names[g.group]] = acc.get(names[g.group], 0) + g.weight
        return max(acc, key=acc.get) if acc else None
    bm = bmesh.new(); bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    uvl = bm.loops.layers.uv["UVMap"]
    for vv in bm.verts:                            # flatten the old hood / jagged tunic back under the cape top
        if vv.co.z > 103.0 and vv.co.y < -10.5: vv.co.y = -10.5 + (vv.co.y + 10.5) * 0.15
        if vv.co.z > 104.0 and abs(vv.co.x) > 8.0 and vv.co.y < -4.0:      # old tunic shoulder points: under the cape top
            vv.co.y = -4.0 + (vv.co.y + 4.0) * 0.55
            if vv.co.z > 111.5: vv.co.z = 111.5 + (vv.co.z - 111.5) * 0.4
    dele = []; counts = {}
    for f in bm.faces:
        p = me.polygons[f.index]; m = mat[f.index].value; d = dom(p) or ""
        zc = sum((me.vertices[v].co.z for v in p.vertices)) / len(p.vertices)
        yc = sum((me.vertices[v].co.y for v in p.vertices)) / len(p.vertices)
        if m in DELETE_MATS or d in ("HEAD", "Neck") or (d == "Chest" and (zc > 113.0 or (zc > 109.5 and yc < -6.0))):
            dele.append(f); col = None
        elif m == 4:
            col = "belt"
            for vv in f.verts:                     # pull the rope belt's back knot in (the cape drapes over it)
                if vv.co.y < -11.0: vv.co.y = -11.0 + (vv.co.y + 11.0) * 0.25
                if abs(vv.co.x) > 12.5: vv.co.x = (12.5 + (abs(vv.co.x) - 12.5) * 0.35) * (1 if vv.co.x > 0 else -1)   # knot ends at his hips
        elif d.endswith("Hand") or d.endswith("ForeArm1") or d.endswith("Foot") or (d.endswith("Calf") and m == 6):
            col = "red"
        else:
            col = "suit"
        if col:
            u, v = suv(col)
            for l in f.loops: l[uvl].uv = (u, v)
            counts[col] = counts.get(col, 0) + 1
    counts["deleted"] = len(dele)
    bmesh.ops.delete(bm, geom=dele, context='FACES')
    bm.to_mesh(me); bm.free(); me.update()
    # drop loose verts left by the delete
    bm = bmesh.new(); bm.from_mesh(me)
    loose = [v for v in bm.verts if not v.link_faces]
    bmesh.ops.delete(bm, geom=loose, context='VERTS'); bm.to_mesh(me); bm.free(); me.update()
    return counts

# ------------------------------------------------------------------------------------------------ helpers
def new_obj(name, verts, faces, uvs, groups):
    """verts: list of Vector; faces: list of index tuples; uvs: per-face list of (u,v) per corner; groups: per-vertex
    dict bone->weight."""
    me = bpy.data.meshes.new(name); me.from_pydata([tuple(v) for v in verts], [], faces); me.update()
    uvl = me.uv_layers.new(name="UVMap")
    for p in me.polygons:
        for k, li in enumerate(p.loop_indices): uvl.data[li].uv = uvs[p.index][k]
    ob = bpy.data.objects.new(name, me); bpy.context.scene.collection.objects.link(ob)
    ob.parent = ARM; mod = ob.modifiers.new("Armature", 'ARMATURE'); mod.object = ARM
    for vi, gw in enumerate(groups):
        for bn, w in gw.items():
            if w <= 0: continue
            vg = ob.vertex_groups.get(bn) or ob.vertex_groups.new(name=bn)
            vg.add([vi], w, 'REPLACE')
    me.materials.append(bpy.data.materials["SP_Palette_Lit"])
    for p in me.polygons: p.use_smooth = True
    return ob

class MeshBuf:
    def __init__(s): s.v, s.f, s.uv, s.g = [], [], [], []
    def ellipsoid(s, c, r, col, bone, seg=24, rings=14, rot=None, keep=None):
        base = len(s.v); u, v = suv(col)
        rot = rot or Matrix.Identity(3)
        for i in range(rings + 1):
            th = math.pi * i / rings
            for j in range(seg):
                ph = 2 * math.pi * j / seg
                p = Vector((r[0] * math.sin(th) * math.cos(ph), r[1] * math.sin(th) * math.sin(ph), r[2] * math.cos(th)))
                s.v.append(c + rot @ p); s.g.append(dict(bone) if isinstance(bone, dict) else {bone: 1.0})
        for i in range(rings):
            for j in range(seg):
                a = base + i * seg + j; b = base + i * seg + (j + 1) % seg; c2 = base + (i + 1) * seg + (j + 1) % seg; d = base + (i + 1) * seg + j
                s.f.append((a, d, c2, b)); s.uv.append([(u, v)] * 4)
    def tube(s, p0, p1, rad, col, bones, seg=16, cap=True):
        base = len(s.v); u, v = suv(col)
        ax = (p1 - p0); L = ax.length; ax.normalize()
        t1 = ax.orthogonal().normalized(); t2 = ax.cross(t1)
        for i, (p, bw) in enumerate(((p0, bones[0]), (p1, bones[1]))):
            for j in range(seg):
                ph = 2 * math.pi * j / seg
                s.v.append(p + (t1 * math.cos(ph) + t2 * math.sin(ph)) * rad); s.g.append(dict(bw))
        for j in range(seg):
            a = base + j; b = base + (j + 1) % seg; c = base + seg + (j + 1) % seg; d = base + seg + j
            s.f.append((a, b, c, d)); s.uv.append([(u, v)] * 4)

# ------------------------------------------------------------------------------------------------ 3. head
HEAD_C = Vector((0.0, 0.4, 125.6)); HEAD_R = (8.1, 8.5, 9.5)
def head_surface_y(x, z):
    k = 1 - (x / HEAD_R[0]) ** 2 - ((z - HEAD_C.z) / HEAD_R[2]) ** 2
    return HEAD_C.y + HEAD_R[1] * math.sqrt(max(0.0, k))
def build_head():
    hb = MeshBuf()
    hb.ellipsoid(HEAD_C, HEAD_R, "skin", "HEAD", seg=32, rings=20)
    # ears
    for sx in (-1, 1):
        hb.ellipsoid(Vector((sx * (HEAD_R[0] - 0.4), HEAD_C.y - 0.6, HEAD_C.z - 0.6)), (1.1, 1.5, 2.3), "skin", "HEAD", seg=12, rings=8)
    # eyes: white ovals with black pupils, Saitama's plain look (no nose); brows; small flat mouth
    for sx in (-1, 1):
        ex, ez = sx * 2.75, HEAD_C.z + 0.9
        ey = head_surface_y(ex, ez)
        n = Vector((ex / HEAD_R[0] ** 2, (ey - HEAD_C.y) / HEAD_R[1] ** 2, (ez - HEAD_C.z) / HEAD_R[2] ** 2)).normalized()
        rot = n.to_track_quat('Y', 'Z').to_matrix()
        hb.ellipsoid(Vector((ex, ey, ez)) - n * 0.15, (1.55, 0.55, 1.05), "eyew", "HEAD", seg=16, rings=8, rot=rot)
        px = ex + sx * 0.05
        hb.ellipsoid(Vector((px, ey, ez - 0.1)) + n * 0.32, (0.42, 0.3, 0.42), "pupil", "HEAD", seg=10, rings=6, rot=rot)
        bz = ez + 2.05; bx = sx * 2.85; by = head_surface_y(bx, bz)
        nb = Vector((bx / HEAD_R[0] ** 2, (by - HEAD_C.y) / HEAD_R[1] ** 2, (bz - HEAD_C.z) / HEAD_R[2] ** 2)).normalized()
        hb.ellipsoid(Vector((bx, by, bz)), (1.35, 0.3, 0.26), "brow", "HEAD", seg=10, rings=6, rot=nb.to_track_quat('Y', 'Z').to_matrix())
    mz = HEAD_C.z - 4.6; my = head_surface_y(0.0, mz)
    nm = Vector((0, (my - HEAD_C.y) / HEAD_R[1] ** 2, (mz - HEAD_C.z) / HEAD_R[2] ** 2)).normalized()
    hb.ellipsoid(Vector((0, my, mz)), (1.25, 0.28, 0.22), "mouth", "HEAD", seg=12, rings=6, rot=nm.to_track_quat('Y', 'Z').to_matrix())
    # neck (skin): Neck bone at the bottom, HEAD at the top
    nb0 = bone_model("Neck").translation; nb1 = bone_model("HEAD").translation
    hb.tube(Vector((0, nb0.y + 1.2, nb0.z - 3.0)), Vector((0, nb1.y + 0.6, nb1.z + 1.0)), 3.9, "skin", ({"Neck": 1.0}, {"HEAD": 0.7, "Neck": 0.3}), seg=18)
    # round suit collar at the base of the neck (hides the cut of the old hood), torus of the suit colour
    cz = nb0.z - 0.6; cy = nb0.y + 1.2; R0, r0 = 5.6, 1.7; base = len(hb.v); u, v = suv("suit"); NS, NT = 24, 10
    for i in range(NS):
        a = 2 * math.pi * i / NS
        for j in range(NT):
            b = 2 * math.pi * j / NT
            rr = R0 + r0 * math.cos(b)
            sb = math.sin(a); ky = 1.08 if sb > 0 else 0.78; hb.v.append(Vector((rr * math.cos(a), cy + rr * sb * ky, cz + 1.4 * sb + r0 * math.sin(b)))); hb.g.append({"Neck": 0.35, "Chest": 0.65})
    for i in range(NS):
        for j in range(NT):
            a0 = base + i * NT + j; a1 = base + i * NT + (j + 1) % NT; b1 = base + ((i + 1) % NS) * NT + (j + 1) % NT; b0 = base + ((i + 1) % NS) * NT + j
            hb.f.append((a0, b0, b1, a1)); hb.uv.append([(u, v)] * 4)
    return new_obj("SuperPeasant_Head", hb.v, hb.f, hb.uv, hb.g)

# ------------------------------------------------------------------------------------------------ zip line + buckle
def body_bvh(obj):
    me = obj.data
    return BVHTree.FromPolygons([v.co.copy() for v in me.vertices], [tuple(p.vertices) for p in me.polygons]), me
def nearest_groups(obj, bvh, me, p):
    loc, nrm, fi, d = bvh.find_nearest(p)
    if fi is None: return {"Chest": 1.0}
    names = {g.index: g.name for g in obj.vertex_groups}
    acc = {}
    poly = me.polygons[fi]; ws = []
    for vi in poly.vertices:
        dd = (me.vertices[vi].co - loc).length; ws.append((vi, 1.0 / (dd + 0.05)))
    tot = sum(w for _, w in ws)
    for vi, w in ws:
        for g in me.vertices[vi].groups: acc[names[g.group]] = acc.get(names[g.group], 0) + g.weight * w / tot
    top = sorted(acc.items(), key=lambda t: -t[1])[:4]; s = sum(w for _, w in top)
    return {k: w / s for k, w in top}
def build_trim(obj):
    bvh, me = body_bvh(obj)
    tb = MeshBuf()
    # zip: a strip down the front centre from the collar to the belt
    pts = []
    for i in range(24):
        z = 113.0 - (113.0 - 81.0) * i / 23
        hit = bvh.ray_cast(Vector((0, 40, z)), Vector((0, -1, 0)))
        if hit[0] is None: continue
        pts.append((hit[0], hit[1]))
    base = len(tb.v); u, v = suv("zip")
    for p, n in pts:
        side = Vector((1, 0, 0))
        off = p + n * 0.22
        for sx in (-0.5, 0.5):
            q = off + side * sx
            tb.v.append(q); tb.g.append(nearest_groups(obj, bvh, me, p))
    for i in range(len(pts) - 1):
        a = base + 2 * i
        tb.f.append((a, a + 1, a + 3, a + 2)); tb.uv.append([(u, v)] * 4)   # faces out of his chest (+y)
    # zip pull tab near the collar
    if pts:
        p, n = pts[1]
        tb.ellipsoid(p + n * 0.5, (0.55, 0.3, 0.9), "zip", nearest_groups(obj, bvh, me, p), seg=10, rings=6)
    # gold belt buckle at the front of the belt
    hit = bvh.ray_cast(Vector((0, 40, 72.8)), Vector((0, -1, 0)))
    if hit[0] is not None:
        tb.ellipsoid(hit[0] + Vector((0, 0.5, 0)), (2.6, 0.7, 2.6), "gold", "Pelvis", seg=18, rings=8)
    return new_obj("SuperPeasant_Trim", tb.v, tb.f, tb.uv, tb.g), len(pts)

# ------------------------------------------------------------------------------------------------ 4. cape
CAPE_BONES = [["Chest_Upper_skin", "Chest_Rear_Skin", "Chest_skin"],          # row 1 (v = 0.25): left, centre, right
              ["Neck_Low_skin", "Spine2_Rear_skin", "Spine2_skin"],          # row 2 (v = 0.5)
              ["UpperBelly_skin", "Spine1_Rear_skin", "Spine1_skin"],        # row 3 (v = 0.75)
              ["Belly_skin", "Pelvis_Rear_skin", "Pelvis_skin"]]             # row 4 (v = 1.0, the hem)
BONE_V = [0.25, 0.5, 0.75, 1.0]; BONE_U = [0.0, 0.5, 1.0]
PU, PV = 6, 10                    # cloth particles across / down (row 0 pinned)
CAPE_TOP_Z = 114.6; CAPE_LEN = 82.0; HEM_WIDEN = 1.40
def back_profile(bvh):
    """most-backward body point (min y) at x, z, from rays shot forward from behind."""
    def yb(x, z):
        hit = bvh.ray_cast(Vector((x, -60, z)), Vector((0, 1, 0)))
        return hit[0].y if hit[0] is not None else None
    return yb
def cape_surface(bvh):
    """bind surface S(u, v) (model space): top arc over the shoulders/upper back, hangs straight down, widens to the hem,
    kept >= 1.6 u behind the body (cumulative: it drapes over the most protruding point above)."""
    yb = back_profile(bvh)
    NUs, NVs = 25, 81
    top_half = 12.8
    grid = [[None] * NUs for _ in range(NVs)]
    run_min = [None] * NUs
    for j in range(NVs):
        v = j / (NVs - 1); z = CAPE_TOP_Z - CAPE_LEN * v
        for i in range(NUs):
            u = i / (NUs - 1); s = (u - 0.5) * 2.0
            half = top_half * (1.0 + (HEM_WIDEN - 1.0) * min(1.0, v * 1.35))
            x = s * half
            # top arc: corners come forward over the shoulders
            ytop = -7.2 + 6.0 * (s * s)
            y = ytop - 1.0 * v
            b = yb(max(-15.5, min(15.5, x)), z)
            if b is not None: y = min(y, b - 1.6)
            if run_min[i] is None or y < run_min[i]: run_min[i] = y
            y = min(y, run_min[i] + 0.0) if v > 0.12 else y
            grid[j][i] = Vector((x, y, z))
    # smooth y along v (no kinks)
    for _ in range(6):
        for j in range(1, NVs - 1):
            for i in range(NUs):
                grid[j][i].y = min(grid[j][i].y, 0.5 * (grid[j - 1][i].y + grid[j + 1][i].y))
    def S(u, v):
        fu = u * (NUs - 1); fv = v * (NVs - 1)
        i0 = min(NUs - 2, int(fu)); j0 = min(NVs - 2, int(fv)); a = fu - i0; b = fv - j0
        return (grid[j0][i0] * (1 - a) * (1 - b) + grid[j0][i0 + 1] * a * (1 - b) + grid[j0 + 1][i0] * (1 - a) * b + grid[j0 + 1][i0 + 1] * a * b)
    return S
def cape_weights(u, v):
    w = {}
    # columns
    if u <= 0.5: c0, c1, fu = 0, 1, u / 0.5
    else: c0, c1, fu = 1, 2, (u - 0.5) / 0.5
    if v <= BONE_V[0]:
        fv = v / BONE_V[0]
        w["Chest"] = 1.0 - fv
        w[CAPE_BONES[0][c0]] = w.get(CAPE_BONES[0][c0], 0) + fv * (1 - fu)
        w[CAPE_BONES[0][c1]] = w.get(CAPE_BONES[0][c1], 0) + fv * fu
    else:
        r0 = max(k for k in range(4) if BONE_V[k] <= v + 1e-9); r0 = min(r0, 2); r1 = r0 + 1
        fv = (v - BONE_V[r0]) / (BONE_V[r1] - BONE_V[r0])
        for (r, wr) in ((r0, 1 - fv), (r1, fv)):
            for (c, wc) in ((c0, 1 - fu), (c1, fu)):
                if wr * wc > 0: w[CAPE_BONES[r][c]] = w.get(CAPE_BONES[r][c], 0) + wr * wc
    return {k: x for k, x in w.items() if x > 1e-4}
def build_cape(obj):
    bvh, me = body_bvh(obj)
    S = cape_surface(bvh)
    NU, NV = 13, 33
    cb = MeshBuf()
    uo, vo = suv("cape"); ui, vi_ = suv("capein")
    THICK = 0.45
    def normal_at(u, v):
        e = 1e-3
        du = S(min(1, u + e), v) - S(max(0, u - e), v); dv = S(u, min(1, v + e)) - S(u, max(0, v - e))
        n = du.cross(dv); n.normalize(); return n          # du = +x, dv = -z -> n = +x cross -z = +y ... sign fixed below
    params = []
    for layer in (0, 1):
        for j in range(NV):
            for i in range(NU):
                u = i / (NU - 1); v = j / (NV - 1)
                p = S(u, v); n = normal_at(u, v)
                if n.y > 0: n = -n                      # n points backward (away from his back)
                q = p + n * (THICK * 0.5 if layer == 0 else -THICK * 0.5)
                cb.v.append(q); cb.g.append(cape_weights(u, v)); params.append((u, v))
    off = NU * NV
    for j in range(NV - 1):
        for i in range(NU - 1):
            a = j * NU + i; b = a + 1; c = a + NU + 1; d = a + NU
            # outer layer faces backward (-y): winding so the normal points away from the body
            cb.f.append((a, d, c, b)); cb.uv.append([(uo, vo)] * 4)
            cb.f.append((off + a, off + b, off + c, off + d)); cb.uv.append([(ui, vi_)] * 4)
    # side + hem edges (close the sheet so no gap shows)
    def edge(ia, ib):
        cb.f.append((ia, ib, off + ib, off + ia)); cb.uv.append([(uo, vo)] * 4)
    for j in range(NV - 1):
        edge(j * NU, (j + 1) * NU)                                   # left
        edge((j + 1) * NU + NU - 1, j * NU + NU - 1)                 # right
    for i in range(NU - 1):
        edge((NV - 1) * NU + i, (NV - 1) * NU + i + 1)               # hem
        edge(i + 1, i)                                               # top
    ob = new_obj("SuperPeasant_Cape", cb.v, cb.f, cb.uv, cb.g)
    # fix outward normals: make every face's normal point away from the cape's mid surface side it belongs to
    me2 = ob.data; me2.update()
    bm = bmesh.new(); bm.from_mesh(me2); bm.faces.ensure_lookup_table()
    flips = 0
    for k, f in enumerate(bm.faces):
        if k < 2 * (NU - 1) * (NV - 1):
            outer = (k % 2 == 0)
            c = f.calc_center_median(); u = sum(params[l.vert.index][0] for l in f.loops) / 4; v = sum(params[l.vert.index][1] for l in f.loops) / 4
            nref = normal_at(u, v)
            if nref.y > 0: nref = -nref
            want = nref if outer else -nref
            if f.normal.dot(want) < 0: f.normal_flip(); flips += 1
    bm.to_mesh(me2); bm.free(); me2.update()
    # cloth particles (sim grid) + bone anchors
    parts = [[list(S(i / (PU - 1), j / (PV - 1))) for i in range(PU)] for j in range(PV)]
    anchors = []
    for r in range(4):
        for c in range(3):
            anchors.append({"bone": CAPE_BONES[r][c], "u": BONE_U[c], "v": BONE_V[r]})
    return ob, {"particles": parts, "anchors": anchors, "flips": flips, "mesh_verts": len(cb.v)}

# ------------------------------------------------------------------------------------------------ collision capsules
def capsules(obj):
    me = obj.data; names = {g.index: g.name for g in obj.vertex_groups}
    def verts_of(bones, pred=lambda co: True):
        out = []
        for v in me.vertices:
            if not v.groups: continue
            g = max(v.groups, key=lambda g: g.weight)
            if names[g.group] in bones and pred(v.co): out.append(v.co.copy())
        return out
    def seg_radius(p0, p1, pts, pct=0.92):
        ax = p1 - p0; L2 = ax.length_squared
        ds = []
        for p in pts:
            t = max(0.0, min(1.0, (p - p0).dot(ax) / L2)) if L2 > 0 else 0.0
            ds.append((p - (p0 + ax * t)).length)
        ds.sort(); return ds[int(len(ds) * pct)] if ds else 5.0
    B = lambda n: bone_model(n).translation
    caps = []
    bvh, _ = body_bvh(obj)
    def yback(x, z):
        hit = bvh.ray_cast(Vector((x, -60, z)), Vector((0, 1, 0)))
        return hit[0].y if hit[0] is not None else None
    # torso: two side-by-side capsules whose BACK surface sits on the measured back of the suit (the cape side matters)
    RT = 10.0
    for sx in (-1, 1):
        ys = [yback(sx * 5.5, z) for z in (86.0, 92.0, 98.0, 104.0, 109.0)]
        ys = [y for y in ys if y is not None]
        yb = sorted(ys)[len(ys) // 2] if ys else -11.0
        p0 = Vector((sx * 5.5, yb + RT, 86.0)); p1 = Vector((sx * 6.0, yb + RT, 109.0))
        caps.append({"bone": "Chest", "p0": list(p0), "p1": list(p1), "r": RT})
    # pelvis / belt: horizontal capsule, back surface on the measured back of the belt
    RP = 10.5
    ys = [y for y in (yback(x, z) for x in (-10.0, -6.0, -2.0, 2.0, 6.0, 10.0) for z in (66.0, 70.0, 74.0, 78.0)) if y is not None]
    yb = min(ys) if ys else -18.0
    caps.append({"bone": "Pelvis", "p0": [-8.5, yb + RP, 71.5], "p1": [8.5, yb + RP, 71.5], "r": RP})
    caps.append({"bone": "SPINE1", "p0": [-8.5, yb + RP, 77.0], "p1": [8.5, yb + RP, 77.0], "r": RP})   # the belt top bends with the spine
    LIMB_R = {"UpperArm": 5.0, "ForeArm1": 5.0, "Thigh": 6.6, "Calf": 5.4}
    for s in ("L", "R"):
        caps.append({"bone": s + "Arm_UpperArm", "p0": list(B(s + "Arm_UpperArm")), "p1": list(B(s + "Arm_ForeArm1")), "r": LIMB_R["UpperArm"]})
        caps.append({"bone": s + "Arm_ForeArm1", "p0": list(B(s + "Arm_ForeArm1")), "p1": list(B(s + "Arm_Hand")), "r": LIMB_R["ForeArm1"]})
        caps.append({"bone": s + "Leg_Thigh", "p0": list(B(s + "Leg_Thigh")), "p1": list(B(s + "Leg_Calf")), "r": LIMB_R["Thigh"]})
        caps.append({"bone": s + "Leg_Calf", "p0": list(B(s + "Leg_Calf")), "p1": list(B(s + "Leg_Foot")), "r": LIMB_R["Calf"]})
    caps.append({"bone": "HEAD", "p0": list(HEAD_C - Vector((0, 0, 2.0))), "p1": list(HEAD_C + Vector((0, 0, 2.0))), "r": max(HEAD_R) - 1.0})
    for c in caps: c["r"] = round(min(c["r"], 16.0), 2)
    return caps

# ------------------------------------------------------------------------------------------------ run
for o in list(bpy.data.objects):
    if o.type == 'MESH' and o.name in ("SuperPeasant_Glow", "SuperPeasant_RigidGlow", "BSBound:BBX"):
        bpy.data.objects.remove(o, do_unlink=True)
for c in bpy.data.collections: c.hide_viewport = False; c.hide_render = False
VARS = {"smooth": "SuperPeasant_Body", "rigid": "SuperPeasant_RigidBody"}
NIF = {"smooth": "SuperPeasant.nif", "rigid": "SuperPeasant_Rigid.nif"}
report["recolour"] = {k: recolour(bpy.data.objects[n]) for k, n in VARS.items()}
smooth = bpy.data.objects["SuperPeasant_Body"]
head = build_head()
cape, cape_info = build_cape(smooth)
report["capsules"] = capsules(smooth)
trims = {}
for var, bn in VARS.items():
    t, nz = build_trim(bpy.data.objects[bn]); t.name = "SuperPeasant_Trim" if var == "smooth" else "SuperPeasant_RigidTrim"; trims[var] = t
    report.setdefault("zip_points", {})[var] = nz
report["cape"] = {k: v for k, v in cape_info.items() if k != "particles"}
bind = {"particles": cape_info["particles"], "anchors": cape_info["anchors"], "capsules": report["capsules"], "PU": PU, "PV": PV,
        "bones_model": {}}
for bn in ["Chest", "Pelvis", "SPINE1", "HEAD", "LArm_UpperArm", "RArm_UpperArm", "LArm_ForeArm1", "RArm_ForeArm1", "LLeg_Thigh", "RLeg_Thigh",
           "LLeg_Calf", "RLeg_Calf"] + [b for row in CAPE_BONES for b in row]:
    m = bone_model(bn); bind["bones_model"][bn] = [list(r) for r in m]

def finish_shaders(path):
    from io_scene_nifly.pyn.pynifly import NifFile
    TEX = "textures\\SuperPeasant\\"
    nif = NifFile(path)
    for s in nif.shapes:
        want = {"Diffuse": TEX + "superpeasant_opm_d.dds", "Normal": TEX + "superpeasant_n.dds",
                "Specular": TEX + ("superpeasant_opm_s.dds" if "Head" in s.name else "superpeasant_s.dds")}
        for slot, tex in want.items(): s.set_texture(slot, tex)
        p = s.shader.properties
        p.Emissive_Mult = 0.0
        if "Head" in s.name:
            p.Spec_Str = 1.6; p.Glossiness = 90.0
            p.Shader_Flags_1 |= 1          # SLSF1_Specular
        else:
            p.Spec_Str = 0.0; p.Glossiness = 1.0
        s.save_shader_attributes()
    nif.save()
    out = {}
    for s in NifFile(path).shapes:
        p = s.shader.properties
        out[s.name] = {"verts": len(s.verts), "tris": len(s.tris), "bones": list(s.bone_names), "textures": dict(s.textures), "spec": p.Spec_Str,
                       "flags1": hex(p.Shader_Flags_1), "flags2": hex(p.Shader_Flags_2)}
    return out

def readback_skin(path, shape_name):
    from io_scene_nifly.pyn.pynifly import NifFile
    nif = NifFile(path)
    for s in nif.shapes:
        if s.name != shape_name: continue
        out = {}
        for bn in s.bone_names:
            xf = s.get_shape_skin_to_bone(bn)
            out[bn] = {"translation": list(xf.translation), "rotation": [list(r) for r in xf.rotation], "scale": xf.scale}
        return out
    return None

if not NO_EXPORT:
    import addon_utils
    addon_utils.enable("io_scene_nifly", default_set=True, persistent=False)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    for var, bn in VARS.items():
        body = bpy.data.objects[bn]
        bpy.ops.object.select_all(action='DESELECT')
        objs = [body, head, cape, trims[var]]
        for o in objs: o.hide_set(False); o.select_set(True)
        bpy.context.view_layer.objects.active = body
        path = os.path.join(MESHD, NIF[var])
        bpy.ops.export_scene.pynifly(filepath=path, target_game='FO4', rename_bones=False, rename_bones_niftools=False,
                                     blender_xf=False, preserve_hierarchy=False, export_all_bones=False, export_pose=False,
                                     export_modifiers=False, export_animations=False, export_colors=False, write_tris=False,
                                     write_bodytri=False, intuit_defaults=False)
        report.setdefault("nif", {})[var] = finish_shaders(path)
        if var == "smooth":
            bind["skin_to_bone"] = readback_skin(path, "SuperPeasant_Cape")
            bind["skin_to_bone_body"] = readback_skin(path, bn)
json.dump(bind, open(os.path.join(OUT, "cape_bind.json"), "w"), indent=1)
json.dump(report, open(os.path.join(OUT, "opm_report.json"), "w"), indent=1)
print("OPMREPORT", json.dumps({k: v for k, v in report.items() if k not in ("capsules",)})[:3000])
print("CAPSULES", json.dumps(report["capsules"]))

if RENDER:
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_WORKBENCH'
    sc.display.shading.light = 'STUDIO'; sc.display.shading.color_type = 'TEXTURE'
    sc.render.resolution_x, sc.render.resolution_y = 520, 760
    for o in bpy.data.objects:
        if o.type == 'MESH': o.hide_render = o.name not in ("SuperPeasant_Body", "SuperPeasant_Head", "SuperPeasant_Cape", "SuperPeasant_Trim")
    cam = bpy.data.objects.new("cam", bpy.data.cameras.new("cam")); sc.collection.objects.link(cam); sc.camera = cam
    cam.data.type = 'ORTHO'; cam.data.ortho_scale = 165
    world = bpy.data.worlds.new("w"); sc.world = world; world.color = (0.32, 0.35, 0.4)
    for nm, loc, rot in (("front", (0, 200, 80), (math.radians(90), 0, math.radians(180))), ("back", (0, -200, 80), (math.radians(90), 0, 0)),
                         ("side", (200, 0, 80), (math.radians(90), 0, math.radians(90))), ("q", (150, 150, 110), (math.radians(80), 0, math.radians(135))), ("neck", (90, -90, 118), (math.radians(85), 0, math.radians(45)))):
        cam.location = loc; cam.rotation_euler = rot; cam.data.ortho_scale = 48 if nm == "neck" else 165
        sc.render.filepath = os.path.join(OUT, "preview_%s.png" % nm)
        bpy.ops.render.render(write_still=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT, "superpeasant_opm.blend"))
