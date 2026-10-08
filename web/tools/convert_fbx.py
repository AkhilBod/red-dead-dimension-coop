# Blender: convert FBX exports to GLB for the web build.
#   Blender -b --python tools/convert_fbx.py -- <out_dir> <in.fbx> [<in.fbx> ...]
import bpy, sys, os
args = sys.argv[sys.argv.index("--") + 1:]
out_dir, sources = args[0], args[1:]
for src in sources:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=src)
    name = os.path.splitext(os.path.basename(src))[0].replace(" ", "_")
    dst = os.path.join(out_dir, name + ".glb")
    bpy.ops.export_scene.gltf(filepath=dst, export_format="GLB", export_apply=True, export_animations=False)
    mins = [min(v[i] for o in bpy.context.scene.objects if o.type == "MESH" for v in [o.matrix_world @ c.co for c in o.data.vertices]) for i in range(3)]
    maxs = [max(v[i] for o in bpy.context.scene.objects if o.type == "MESH" for v in [o.matrix_world @ c.co for c in o.data.vertices]) for i in range(3)]
    print("CONVERTED", name, "size", [round(maxs[i] - mins[i], 2) for i in range(3)], "min", [round(m, 2) for m in mins])
