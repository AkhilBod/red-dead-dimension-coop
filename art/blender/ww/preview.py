"""Quick preview renders so poses and silhouettes can be checked without the viewport."""
import bpy
from mathutils import Vector


def _cam():
    cam = bpy.data.objects.get("WW_PreviewCam")
    if not cam:
        cam = bpy.data.objects.new("WW_PreviewCam", bpy.data.cameras.new("WW_PreviewCam"))
        bpy.context.scene.collection.objects.link(cam)
    return cam


def _sun():
    if not bpy.data.objects.get("WW_Sun"):
        l = bpy.data.lights.new("WW_Sun", "SUN")
        l.energy = 3.0
        o = bpy.data.objects.new("WW_Sun", l)
        o.rotation_euler = (0.9, 0.0, -0.7)
        bpy.context.scene.collection.objects.link(o)


def shot(path, target=(0, 0, 1.0), dist=4.5, yaw=25.0, pitch=8.0, ortho=None, res=(900, 900), frame=None):
    import math
    sc = bpy.context.scene
    if frame is not None:
        sc.frame_set(frame)
    cam = _cam()
    _sun()
    t = Vector(target)
    y, p = math.radians(yaw), math.radians(pitch)
    off = Vector((math.sin(y) * math.cos(p), -math.cos(y) * math.cos(p), math.sin(p))) * dist
    cam.location = t + off
    cam.rotation_euler = (t - cam.location).to_track_quat("-Z", "Y").to_euler()
    if ortho:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = "PERSP"
        cam.data.lens = 60
    sc.camera = cam
    sc.render.engine = "BLENDER_WORKBENCH"
    sh = sc.display.shading
    sh.light = "STUDIO"
    sh.color_type = "MATERIAL"
    sh.show_shadows = False
    sh.show_cavity = True
    sh.show_object_outline = False
    sc.render.resolution_x, sc.render.resolution_y = res
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    sc.render.filepath = path
    sc.render.image_settings.file_format = "PNG"
    try:
        sc.world.color = (0.66, 0.84, 0.78)
    except Exception:
        pass
    for o in bpy.data.objects:
        if o.type == "ARMATURE":
            o.hide_render = True
    bpy.ops.render.render(write_still=True)
    return path


def sheet(arm, shots, outdir, prefix, target=(0, 0, 0.95), dist=5.0, yaw=35, pitch=8, res=(420, 420)):
    """shots: list of (action_name, frame). Renders one tile per shot; montage them outside Blender."""
    from .core import set_action
    paths = []
    for i, (an, fr) in enumerate(shots):
        set_action(arm, bpy.data.actions[an])
        t = Vector(target) + arm.location
        p = f"{outdir}{prefix}_{i:02d}_{an}_{fr}.png"
        shot(p, target=tuple(t), dist=dist, yaw=yaw, pitch=pitch, res=res, frame=fr)
        paths.append(p)
    return paths
