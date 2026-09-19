"""Build the whole enemy roster, lay out a showcase, and export for Unreal.

Run inside Blender:
    import sys; sys.path.insert(0, "<repo>/art/blender"); import ww.build_all as b; b.build(); b.export_all("<repo>/art/export")
"""
import os

import bpy

from . import anims, core, horse, human, props, variants

SHOWCASE = {"Bandit": "aim", "Gunslinger": "dual_aim", "Rifleman": "rifle_aim", "Deputy": "idle", "Dynamiter": "throw",
            "Heavy": "gatling_fire", "Boss": "hip_aim"}


def build():
    core.wipe()
    sc = bpy.context.scene
    sc.render.fps = core.FPS
    sc.frame_start, sc.frame_end = 0, 60
    out = {"humans": {}, "horses": {}}
    x = -4.2
    for name, cfg in variants.VARIANTS.items():
        arm, parts = human.build_human(name, cfg, location=(x, 0, 0))
        out["humans"][name] = (arm, parts)
        x += 1.4
    first = next(iter(out["humans"].values()))[0]
    acts = anims.build_human_actions(first)
    for name, (arm, _) in out["humans"].items():
        core.set_action(arm, acts[SHOWCASE.get(name, "idle")])
    # gatling in front of the heavy
    hv = out["humans"]["Heavy"][0]
    g, gp = props.build_gatling("Gatling", location=(hv.location.x, hv.location.y - 0.88, 0))
    out["gatling"] = (g, gp)
    # horses
    x = -4.0
    hacts = None
    for name, cfg in variants.HORSES.items():
        arm, parts = horse.build_horse(name, cfg, location=(x, 5.0, 0))
        out["horses"][name] = (arm, parts)
        if hacts is None:
            hacts = horse.build_horse_actions(arm)
        core.set_action(arm, hacts["idle"])
        x += 2.2
    # mounted demo
    cfg = dict(variants.VARIANTS["Bandit"])
    rider, rparts = human.build_human("Demo_Rider", cfg)
    mount_h, mparts = horse.build_horse("Demo_Horse", variants.HORSES["Horse_Bay"], location=(6.5, 1.0, 0))
    bpy.context.view_layer.update()
    horse.mount(rider, mount_h)
    core.set_action(rider, acts["ride_aim_left"])
    core.set_action(mount_h, hacts["gallop"])
    out["demo"] = (rider, mount_h)
    # standalone props
    pc = core.get_collection("WW_Props")
    px = -4.0
    for nm, fn, kw in (("Prop_Revolver", props.revolver, {}), ("Prop_Revolver_Long", props.revolver, {"long_barrel": True}),
                       ("Prop_Rifle", props.rifle, {}), ("Prop_Shotgun", props.rifle, {"shotgun": True}),
                       ("Prop_Dynamite", props.dynamite, {}), ("Prop_DynamiteStick", props.dynamite, {"sticks": 1})):
        ob = props.standalone(nm, fn, pc, scale=props.GUN_SCALE, **kw)
        ob.location = (px, -3.0, 0.5)
        px += 0.9
    hat_c = core.get_collection("WW_Props")
    for name, cfg in variants.VARIANTS.items():
        mb = core.MB(f"Prop_Hat_{name}")
        human.build_hat(mb, cfg)
        ob = mb.finish(hat_c)
        for v in ob.data.vertices:
            v.co.z -= 1.80
        ob.location = (px, -3.0, 0.3)
        px += 0.7
    return out


def _select(obs):
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in obs:
        o.hide_set(False)
        o.select_set(True)
    bpy.context.view_layer.objects.active = obs[0]


def _nla(arm, actions):
    ad = arm.animation_data_create()
    saved = ad.action
    ad.action = None
    for t in list(ad.nla_tracks):
        ad.nla_tracks.remove(t)
    for a in actions:
        tr = ad.nla_tracks.new()
        tr.name = a.name
        st = tr.strips.new(a.name, int(a.frame_range[0]), a)
        st.name = a.name
    return saved


def _clear_nla(arm, saved):
    ad = arm.animation_data
    for t in list(ad.nla_tracks):
        ad.nla_tracks.remove(t)
    core.set_action(arm, saved)


def export_rig(arm, path_noext, actions=None, glb=True, fbx_anims=True):
    meshes = [o for o in arm.children if o.type == "MESH"]
    real_name = arm.name
    loc = arm.location.copy()
    cons = [(c, c.mute) for c in arm.constraints]
    for c, _ in cons:
        c.mute = True
    arm.location = (0, 0, 0)
    saved = _nla(arm, actions or [])
    other = bpy.data.objects.get("Armature")
    if other and other is not arm:
        other.name = "Armature_tmp"
    arm.name = "Armature"
    bpy.context.view_layer.update()
    _select([arm] + meshes)
    try:
        bpy.ops.export_scene.fbx(filepath=path_noext + ".fbx", use_selection=True, object_types={"ARMATURE", "MESH"},
                                 add_leaf_bones=False, bake_anim=bool(actions) and fbx_anims, bake_anim_use_all_bones=True,
                                 bake_anim_use_nla_strips=True, bake_anim_use_all_actions=False, bake_anim_force_startend_keying=True,
                                 bake_anim_simplify_factor=0.0, mesh_smooth_type="FACE", use_armature_deform_only=False,
                                 axis_forward="-Y", axis_up="Z", apply_scale_options="FBX_SCALE_NONE")
        if glb:
            bpy.ops.export_scene.gltf(filepath=path_noext + ".glb", use_selection=True, export_format="GLB", export_animations=bool(actions),
                                      export_animation_mode="NLA_TRACKS", export_yup=True, export_apply=False)
    finally:
        arm.name = real_name
        arm.location = loc
        _clear_nla(arm, saved)
        for c, m in cons:
            c.mute = m


def export_static(ob, path_noext):
    loc = ob.location.copy()
    ob.location = (0, 0, 0)
    _select([ob])
    try:
        bpy.ops.export_scene.fbx(filepath=path_noext + ".fbx", use_selection=True, object_types={"MESH"}, bake_anim=False,
                                 mesh_smooth_type="FACE", axis_forward="-Y", axis_up="Z")
    finally:
        ob.location = loc


def export_all(outdir):
    for sub in ("characters", "horses", "props"):
        os.makedirs(os.path.join(outdir, sub), exist_ok=True)
    human_acts = [bpy.data.actions[n] for n in anims.NAMES]
    horse_acts = [bpy.data.actions["horse_" + n] for n in ("idle", "walk", "gallop", "rear", "death")]
    done = []
    for name in variants.VARIANTS:
        # FBX animation bakes are ~11 MB each. Same-scale variants reuse the Bandit's; the GLBs always carry everything.
        full = name == "Bandit" or variants.VARIANTS[name].get("scale", 1.0) != 1.0
        export_rig(bpy.data.objects[name], os.path.join(outdir, "characters", "SK_" + name), human_acts, fbx_anims=full)
        done.append(name)
    for name in variants.HORSES:
        export_rig(bpy.data.objects[name], os.path.join(outdir, "horses", "SK_" + name), horse_acts)
        done.append(name)
    export_rig(bpy.data.objects["Gatling"], os.path.join(outdir, "props", "SK_Gatling"), [bpy.data.actions["gatling_spin"]])
    for ob in bpy.data.objects:
        if ob.name.startswith("Prop_"):
            export_static(ob, os.path.join(outdir, "props", "SM_" + ob.name[5:]))
            done.append(ob.name)
    return done
