# Fit the TABS-proportioned Super Peasant onto the FO4 human skeleton, re-name his weights to FO4 bones and export FO4 NIFs.
#
# Real run (needs PyNifly installed for this Blender and a FO4 skeleton.nif: PyNifly bundles one at
# io_scene_nifly\skeletons\FO4\skeleton.nif, the game has Data\Meshes\Actors\Character\CharacterAssets\skeleton.nif in a BA2):
#   blender.exe -b fo4\model\blender\superpeasant_fo4_prep.blend --python fo4\model\blender\fit_export_fo4.py -- ^
#       --skeleton <path\skeleton.nif> --variant both [--out-dir fo4\build\Data\Meshes\SuperPeasant (default)] [--verify]
#   (--verify then runs verify_nif_fo4.py in a second Blender: re-import checks + pose renders, model\nif_verify.json)
# Self-test of the maths without PyNifly (PLACEHOLDER skeleton, NOT game data, nothing exported):
#   blender.exe -b fo4\model\blender\superpeasant_fo4_prep.blend --python fo4\model\blender\fit_export_fo4.py -- --placeholder --variant both
#
# Steps: 1 import skeleton.nif (PyNifly, rename_bones=False so bones keep their NIF names) and dump it to fo4_skeleton_dump.json
#        2 axis check (left hand at -X, toes toward +Y, head above pelvis), else stop
#        3 uniform scale k = FO4 Neck height / TABS neck height
#        4 pose TABS_Rig per bone_map_fo4.json "fit_chain" (align = TABS joint onto FO4 joint + swing + optional stretch along the bone)
#        5 deform the meshes with that pose (positions and normals), bake it as the new rest shape
#        6 rename vertex groups to FO4 bones (option 1 also splits M_Torso into SPINE2/Chest and the upper M_Hip into SPINE1), max 4
#        7 bind to the FO4 armature and export with bpy.ops.export_scene.pynifly(target_game='FO4')
import bpy, sys, os, json, math
from mathutils import Vector, Matrix

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def opt(name, default=None, flag=False):
    if name not in argv: return default
    return True if flag else argv[argv.index(name) + 1]
SKEL = opt("--skeleton"); PLACEHOLDER = opt("--placeholder", False, True); VARIANT = opt("--variant", "both")
VERIFY = opt("--verify", False, True); NO_EXPORT = opt("--no-export", False, True) or PLACEHOLDER
HERE = os.path.dirname(os.path.abspath(__file__)); MODEL = os.path.normpath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True; sys.path.insert(0, HERE); from shortpath import short          # nifly DLL I/O must use paths under MAX_PATH
OUT_DIR = opt("--out-dir", os.path.join(MODEL, "..", "build", "Data", "Meshes", "SuperPeasant"))
STRETCH_CLAMP = (0.6, 1.6)
bmap = json.load(open(os.path.join(MODEL, "bone_map_fo4.json")))
prep = json.load(open(os.path.join(MODEL, "superpeasant_fo4_prep.json")))
report = {"mode": "placeholder-selftest" if PLACEHOLDER else "skeleton", "skeleton": SKEL, "warnings": []}
def smoothstep(a, b, x):
    if b == a: return 1.0 if x >= b else 0.0
    t = min(max((x - a) / (b - a), 0.0), 1.0); return t * t * (3 - 2 * t)

# ---------------------------------------------------------------- 1. FO4 armature
def placeholder_armature():
    """SELF-TEST ONLY. Rough human proportions (128 u tall, arms 30 deg below horizontal), NOT read from Fallout 4."""
    P = {"Root": (0, 0, 0), "COM": (0, 0, 66), "Pelvis": (0, 0, 66), "SPINE1": (0, 0, 72), "SPINE2": (0, 0, 80), "Chest": (0, 0, 90),
         "Neck": (0, -1, 106), "HEAD": (0, 0, 113)}
    par = {"COM": "Root", "Pelvis": "COM", "SPINE1": "COM", "SPINE2": "SPINE1", "Chest": "SPINE2", "Neck": "Chest", "HEAD": "Neck"}
    a = math.radians(30)
    for s, sx in (("L", -1), ("R", 1)):
        sh = Vector((sx * 12, 0, 103)); d = Vector((sx * math.cos(a), 0, -math.sin(a)))
        P[s + "Arm_Collarbone"] = (sx * 3, 0, 103); P[s + "Arm_UpperArm"] = tuple(sh); P[s + "Arm_ForeArm1"] = tuple(sh + d * 19)
        P[s + "Arm_Hand"] = tuple(sh + d * 36); P[s + "Arm_Finger31"] = tuple(sh + d * 42)
        P[s + "Leg_Thigh"] = (sx * 6, 0, 63); P[s + "Leg_Calf"] = (sx * 6.5, 1, 35); P[s + "Leg_Foot"] = (sx * 7, -1, 6); P[s + "Leg_Toe1"] = (sx * 7, 9, 1.5)
        par.update({s + "Arm_Collarbone": "Chest", s + "Arm_UpperArm": s + "Arm_Collarbone", s + "Arm_ForeArm1": s + "Arm_UpperArm",
                    s + "Arm_Hand": s + "Arm_ForeArm1", s + "Arm_Finger31": s + "Arm_Hand", s + "Leg_Thigh": "Pelvis",
                    s + "Leg_Calf": s + "Leg_Thigh", s + "Leg_Foot": s + "Leg_Calf", s + "Leg_Toe1": s + "Leg_Foot"})
    arm = bpy.data.armatures.new("FO4_PLACEHOLDER_NOT_GAME_DATA"); ob = bpy.data.objects.new(arm.name, arm)
    bpy.context.scene.collection.objects.link(ob); bpy.context.view_layer.objects.active = ob
    bpy.ops.object.mode_set(mode='EDIT')
    kids = {}
    for c, p in par.items(): kids.setdefault(p, []).append(c)
    for n, h in P.items():
        eb = arm.edit_bones.new(n); eb.head = Vector(h)
        ch = kids.get(n); eb.tail = Vector(P[ch[0]]) if ch and (Vector(P[ch[0]]) - Vector(h)).length > 0.5 else Vector(h) + Vector((0, 0, 3))
    for c, p in par.items(): arm.edit_bones[c].parent = arm.edit_bones[p]
    bpy.ops.object.mode_set(mode='OBJECT')
    return ob

def import_skeleton(path):
    import addon_utils
    addon_utils.enable("io_scene_nifly", default_set=True, persistent=False)
    before = set(bpy.data.objects)
    kw = dict(filepath=short(path), rename_bones=False, create_bones=False, blender_xf=False, import_animations=False, import_collisions=False)
    try:
        bpy.ops.import_scene.pynifly(**kw)
    except TypeError as e:                       # an option name changed in a newer PyNifly: retry with the documented minimum
        report["warnings"].append("import option fallback: %r" % e)
        bpy.ops.import_scene.pynifly(filepath=short(path), rename_bones=False, blender_xf=False)
    arms = [o for o in bpy.data.objects if o not in before and o.type == 'ARMATURE']
    if not arms: raise SystemExit("PyNifly created no armature from " + path)
    return max(arms, key=lambda o: len(o.data.bones))

fo4 = placeholder_armature() if PLACEHOLDER else import_skeleton(SKEL)
J = {b.name: fo4.matrix_world @ b.head_local for b in fo4.data.bones}
dump = [{"name": b.name, "parent": b.parent.name if b.parent else None, "head": [round(x, 4) for x in fo4.matrix_world @ b.head_local],
         "tail": [round(x, 4) for x in fo4.matrix_world @ b.tail_local], "matrix_local": [[round(x, 6) for x in r] for r in b.matrix_local]}
        for b in fo4.data.bones]
json.dump({"source": "PLACEHOLDER (self-test)" if PLACEHOLDER else SKEL, "armature_matrix_world": [list(r) for r in fo4.matrix_world],
           "bones": dump}, open(os.path.join(MODEL, "fo4_skeleton_dump%s.json" % ("_PLACEHOLDER" if PLACEHOLDER else "")), "w"), indent=1)
report["fo4_bone_count"] = len(dump)

# ---------------------------------------------------------------- 2. axis check
need = ["Pelvis", "SPINE1", "SPINE2", "Chest", "Neck", "HEAD"] + [s + n for s in "LR" for n in
        ("Arm_UpperArm", "Arm_ForeArm1", "Arm_Hand", "Leg_Thigh", "Leg_Calf", "Leg_Foot", "Leg_Toe1")]
missing = [n for n in need if n not in J]
if missing: raise SystemExit("FO4 skeleton lacks bones %s (was it imported with rename_bones=False?)" % missing)
checks = {"left_hand_at_minus_x": J["LArm_Hand"].x < 0 < J["RArm_Hand"].x, "toes_forward_plus_y": J["LLeg_Toe1"].y > J["LLeg_Foot"].y,
          "head_above_pelvis": J["HEAD"].z > J["Pelvis"].z + 20}
report["axis_checks"] = checks
if not all(checks.values()): raise SystemExit("axis assumption failed %s: fix P in prep_fo4_model.py before fitting" % checks)

# ---------------------------------------------------------------- 3./4. pose the TABS rig onto the FO4 joints
rig = bpy.data.objects["TABS_Rig"]
T = {b.name: (b.head_local.copy(), b.tail_local.copy()) for b in rig.data.bones}
k = J["Neck"].z / T["M_Neck"][0].z
report["uniform_scale_k"] = k
report["fo4_vs_tabs_heights_u"] = {"Neck": [J["Neck"].z, T["M_Neck"][0].z * k], "Pelvis": [J["Pelvis"].z, T["M_Hip"][0].z * k],
                                   "LLeg_Calf(knee)": [J["LLeg_Calf"].z, T["M_Knee_Left"][0].z * k]}
targets, per_bone = {}, {}
def ground_point(foot_bone):
    """FO4 foot x/y at the scaled TABS foot-joint height (keeps the TABS soles on the floor)."""
    tabs_foot = "M_Foot_Left" if foot_bone.startswith("L") else "M_Foot_Right"
    f = J[foot_bone]; return Vector((f.x, f.y, k * T[tabs_foot][0].z))
for e in bmap["fit_chain"]:
    n, mode, fh, fd, stretch = e["tabs_bone"], e["mode"], e["fo4_head"], e["fo4_dir"], e["stretch"]
    b = rig.data.bones[n]; rest = b.matrix_local.copy(); R4 = rest.to_3x3().normalized().to_4x4()
    if fd and fd.endswith("@ground"):
        J[fd] = ground_point(fd.split("@")[0])
    if mode == "align" and fd not in J:
        report["warnings"].append("%s: FO4 bone %s missing, direction taken from %s's parent chain" % (n, fd, fh))
        fd = None
    if mode == "align":
        d_t = (T[n][1] - T[n][0]); ht = J[fh]
        d_f = (J[fd] - J[fh]) if fd else None
        if d_f is None:                                   # keep the TABS direction after the parent's swing
            p = b.parent.name; d_f = (targets[p] @ rig.data.bones[p].matrix_local.inverted()).to_3x3() @ d_t
        q = d_t.normalized().rotation_difference(d_f.normalized())
        s = d_f.length / (k * d_t.length) if stretch else 1.0
        if not STRETCH_CLAMP[0] <= s <= STRETCH_CLAMP[1]:
            report["warnings"].append("%s stretch %.3f clamped" % (n, s)); s = min(max(s, STRETCH_CLAMP[0]), STRETCH_CLAMP[1])
        M = Matrix.Translation(ht) @ q.to_matrix().to_4x4() @ R4 @ Matrix.Diagonal((k, k * s, k, 1.0))
        per_bone[n] = {"mode": mode, "fo4_head": fh, "fo4_dir": fd, "swing_deg": round(math.degrees(q.angle), 2), "stretch": round(s, 4)}
    elif mode == "follow":
        p = b.parent.name; M = targets[p] @ rig.data.bones[p].matrix_local.inverted() @ rest
        per_bone[n] = {"mode": mode, "parent": p}
    elif mode == "ground":
        f = J[fh]; M = Matrix.Translation(ground_point(fh)) @ R4 @ Matrix.Diagonal((k, k, k, 1.0))
        per_bone[n] = {"mode": mode, "fo4_xy_from": fh, "z_u": round(k * T[n][0].z, 3), "fo4_joint_z_u": round(f.z, 3)}
    targets[n] = M
bpy.context.view_layer.objects.active = rig; bpy.ops.object.mode_set(mode='POSE')
for e in bmap["fit_chain"]:
    rig.pose.bones[e["tabs_bone"]].matrix = targets[e["tabs_bone"]]; bpy.context.view_layer.update()
bpy.ops.object.mode_set(mode='OBJECT')
err = {n: max(abs(a - b) for ra, rb in zip(rig.pose.bones[n].matrix, targets[n]) for a, b in zip(ra, rb)) for n in targets}
report["pose_matrix_error_max"] = max(err.values()); report["bones"] = per_bone
joint_err = {}
for e in bmap["fit_chain"]:
    if e["mode"] == "align": joint_err[e["tabs_bone"]] = round((rig.pose.bones[e["tabs_bone"]].head - J[e["fo4_head"]]).length, 5)
report["posed_joint_to_fo4_joint_error_u"] = joint_err
DEF = {pb.name: pb.matrix @ rig.data.bones[pb.name].matrix_local.inverted() for pb in rig.pose.bones}

# ---------------------------------------------------------------- 5./6./7. per variant: bake, re-weight, bind, export
G1, G2 = bmap["group_to_fo4_option1"], bmap["group_to_fo4_option2"]
def bake_and_reweight(obj_name, shape_key, variant):
    ob = bpy.data.objects[obj_name]; me = ob.data; S = prep["shapes"][shape_key]
    gname = {g.index: g.name for g in ob.vertex_groups}
    newpos, newnrm, neww, pruned, dropped = [], [], [], 0, 0.0
    for v in me.vertices:
        ws = [(gname[g.group], g.weight) for g in v.groups if g.weight > 0]
        A = Matrix(((0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0), (0, 0, 0, 0)))
        for g, w in ws: A = A + DEF[g] * w
        p = A @ v.co; n = (A.to_3x3().inverted().transposed() @ Vector(S["normals"][v.index])).normalized()
        newpos.append(p); newnrm.append(n)
        out = {}
        for g, w in ws:
            t = (G1 if variant == "smooth" else G2)[g]
            if t == "@spine_split":
                c = smoothstep(J["SPINE2"].z, J["Chest"].z, p.z)
                out["Chest"] = out.get("Chest", 0) + w * c; out["SPINE2"] = out.get("SPINE2", 0) + w * (1 - c)
            elif variant == "smooth" and g == "M_Hip":
                u = smoothstep(J["SPINE1"].z, J["SPINE2"].z, p.z)
                out["SPINE1"] = out.get("SPINE1", 0) + w * u; out["Pelvis"] = out.get("Pelvis", 0) + w * (1 - u)
            else:
                out[t] = out.get(t, 0) + w
        lst = sorted(((b, w) for b, w in out.items() if w > 1e-5), key=lambda x: -x[1])
        if len(lst) > 4:                                                    # BSTriShape: 4 bone weights per vertex (nif.xml BSVertexData)
            pruned += 1; dropped = max(dropped, sum(w for _, w in lst[4:])); lst = lst[:4]
        tot = sum(w for _, w in lst); neww.append([(b, w / tot) for b, w in lst])
    for v, p in zip(me.vertices, newpos): v.co = p
    me.normals_split_custom_set_from_vertices([tuple(n) for n in newnrm])
    for g in list(ob.vertex_groups): ob.vertex_groups.remove(g)
    vg = {}
    for vi, lst in enumerate(neww):
        for b, w in lst:
            if b not in J: raise SystemExit("weight target %s is not a bone of the FO4 skeleton" % b)
            if b not in vg: vg[b] = ob.vertex_groups.new(name=b)
            vg[b].add([vi], w, 'REPLACE')
    for m in list(ob.modifiers): ob.modifiers.remove(m)
    mw = ob.matrix_world.copy(); ob.parent = fo4; ob.matrix_world = mw
    mod = ob.modifiers.new("Armature", 'ARMATURE'); mod.object = fo4
    zs = [p.z for p in newpos]
    with open(os.path.join(MODEL, "fitted_%s%s.obj" % (obj_name, "_PLACEHOLDER" if PLACEHOLDER else "")), "w", newline="\n") as fh:
        fh.write("# fitted %s, FO4 actor space (+X right, +Y forward, +Z up)\n" % obj_name)
        for p in newpos: fh.write("v %.4f %.4f %.4f\n" % tuple(p))
        for poly in me.polygons: fh.write("f %s\n" % " ".join(str(i + 1) for i in poly.vertices))
    return {"vertices": len(me.vertices), "groups": sorted(vg), "pruned_to_4": pruned, "max_weight_dropped": round(dropped, 4),
            "z_range_u": [round(min(zs), 3), round(max(zs), 3)]}

TEXDIR = "textures\\SuperPeasant\\"
GLOW_SRGB = (1.0, 129 / 255.0, 0.0)          # Glow_OrangeFantasy 255,129,0 (pupils M_Angel_Gluwu 255,124,16 share the shape)
def finish_shaders(path):
    """NIF-only shaders (no BGSM): pin the three texture paths, no specular shine (TABS _Glossiness 0) and a constant orange
    self-illumination on the glow shape (Own_Emit is already set by PyNifly). Re-reads the file to confirm."""
    from io_scene_nifly.pyn.pynifly import NifFile
    want = {"Diffuse": TEXDIR + "superpeasant_red_d.dds", "Normal": TEXDIR + "superpeasant_n.dds", "Specular": TEXDIR + "superpeasant_s.dds"}
    nif = NifFile(short(path))
    for s in nif.shapes:
        for slot, tex in want.items(): s.set_texture(slot, tex)
        p = s.shader.properties
        p.Spec_Str = 0.0; p.Glossiness = 1.0
        if "Glow" in s.name:
            p.Emissive_Color[0], p.Emissive_Color[1], p.Emissive_Color[2], p.Emissive_Color[3] = GLOW_SRGB + (1.0,)
            p.Emissive_Mult = 2.0
        else:
            p.Emissive_Mult = 0.0
        s.save_shader_attributes()
    nif.save()
    out = {}
    for s in NifFile(short(path)).shapes:
        p = s.shader.properties
        out[s.name] = {"block": s.blockname, "shader_name": s.shader.name, "textures": dict(s.textures),
                       "flags1": hex(p.Shader_Flags_1), "flags2": hex(p.Shader_Flags_2), "own_emit": bool(p.Shader_Flags_1 & (1 << 22)),
                       "emissive": [round(x, 4) for x in p.Emissive_Color], "emissive_mult": p.Emissive_Mult, "spec_str": p.Spec_Str}
        if {k: v for k, v in s.textures.items() if k in want} != want: raise SystemExit("texture paths not written: %r" % s.textures)
    return out

VARS = {"smooth": [("SP_Smooth_Lit", "smooth_lit", "SuperPeasant_Body"), ("SP_Smooth_Glow", "smooth_glow", "SuperPeasant_Glow")],
        "rigid": [("SP_Rigid_Lit", "rigid_lit", "SuperPeasant_RigidBody"), ("SP_Rigid_Glow", "rigid_glow", "SuperPeasant_RigidGlow")]}
NIF = {"smooth": "SuperPeasant.nif", "rigid": "SuperPeasant_Rigid.nif"}
report["variants"] = {}
for c in bpy.data.collections: c.hide_viewport = False; c.hide_render = False      # the prep .blend hides the rigid collection
for var in (["smooth", "rigid"] if VARIANT == "both" else [VARIANT]):
    rv = report["variants"][var] = {}
    objs = []
    for on, sk, final in VARS[var]:
        rv[final] = bake_and_reweight(on, sk, var); ob = bpy.data.objects[on]; ob.name = final; objs.append(ob)
    bpy.ops.object.select_all(action='DESELECT')       # selection = what PyNifly exports; the armature is found via the modifier
    for o in objs:                                      # (selecting the armature too would also pull in the other variant's meshes)
        o.hide_set(False); o.select_set(True)
        if o.data.shape_keys: raise SystemExit("%s has shape keys: PyNifly would write one NIF per key" % o.name)
    bpy.context.view_layer.objects.active = objs[0]
    rv["selected_for_export"] = sorted(o.name for o in bpy.context.selected_objects)
    if not NO_EXPORT:
        os.makedirs(OUT_DIR, exist_ok=True); OUT_DIR = short(OUT_DIR); path = os.path.join(OUT_DIR, NIF[var])
        stray = [f for f in os.listdir(OUT_DIR) if f.startswith(os.path.splitext(NIF[var])[0] + "_")]
        # intuit_defaults=False: PyNifly 29 otherwise overwrites these kwargs from its preferences (rename_bones defaults True)
        bpy.ops.export_scene.pynifly(filepath=short(path), target_game='FO4', rename_bones=False, rename_bones_niftools=False,
                                     blender_xf=False, preserve_hierarchy=False, export_all_bones=False, export_pose=False,
                                     export_modifiers=False, export_animations=False, export_colors=False, write_tris=False,
                                     write_bodytri=False, intuit_defaults=False)
        extra = [f for f in os.listdir(OUT_DIR) if f.startswith(os.path.splitext(NIF[var])[0] + "_") and f not in stray]
        if extra: raise SystemExit("PyNifly wrote unexpected extra files %s" % extra)
        if not os.path.exists(path): raise SystemExit("PyNifly did not write " + path)
        rv["nif"] = "build/Data/Meshes/SuperPeasant/" + NIF[var]; rv["shader_finish"] = finish_shaders(path)
        rv["nif_bytes"] = os.path.getsize(path)
    for o in objs: o.select_set(False); o.hide_set(True)
rig.hide_set(True)
for o in bpy.data.objects:
    if o.type == 'MESH': o.hide_set(False)
bpy.context.preferences.filepaths.save_version = 0      # no .blend1 backup next to it (not saved to the prefs)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(HERE, "superpeasant_fo4_fitted%s.blend" % ("_PLACEHOLDER" if PLACEHOLDER else "")))
json.dump(report, open(os.path.join(MODEL, "fit_report%s.json" % ("_PLACEHOLDER" if PLACEHOLDER else "")), "w"), indent=1)
print(json.dumps({k: v for k, v in report.items() if k not in ("bones",)}, indent=1))
if VERIFY and not NO_EXPORT:              # separate Blender: re-import both NIFs on the skeleton, checks + pose renders (verify_nif_fo4.py)
    import subprocess
    r = subprocess.run([bpy.app.binary_path, "-b", "--python", os.path.join(HERE, "verify_nif_fo4.py"), "--", "--skeleton", SKEL])
    if r.returncode: raise SystemExit("verify_nif_fo4.py exited %d" % r.returncode)
