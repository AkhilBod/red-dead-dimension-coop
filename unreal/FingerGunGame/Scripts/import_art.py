"""Import art/export/*.fbx into /Game/IronHorse. Run headless:
UnrealEditor-Cmd FingerGunGame.uproject -run=pythonscript -script=Scripts/import_art.py (set FG_ART to import from somewhere else)
Re-running skips what already exists (delete Content/IronHorse/<folder> to force)."""
import json, os, sys, glob
import unreal

# This project sits at <repo>/unreal/FingerGunGame, the FBX files at <repo>/art/export.
SRC = os.environ.get("FG_ART", os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "../../../art/export")))
ROOT = "/Game/IronHorse"
# legacy FBX importer asserts without Slate in a commandlet, so this uses Interchange
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary
report = {"meshes": {}, "anims": {}, "errors": []}

def ui_base():
    ui = unreal.FbxImportUI()
    ui.automated_import_should_detect_type = False
    ui.import_materials = True
    ui.import_textures = False
    ui.texture_import_data.material_search_location = unreal.MaterialSearchLocation.ALL_ASSETS
    return ui

def run(path, dest, ui):
    t = unreal.AssetImportTask()
    t.filename, t.destination_path, t.options = path, dest, ui
    t.automated, t.save, t.replace_existing = True, True, True
    tools.import_asset_tasks([t])
    return [str(p) for p in t.imported_object_paths]

def static(folder, combine=True, complex_collision=False):
    for f in sorted(glob.glob(f"{SRC}/{folder}/SM_*.fbx")):
        name = os.path.splitext(os.path.basename(f))[0]
        dest = f"{ROOT}/{folder}"
        if eal.does_asset_exist(f"{dest}/{name}"):
            continue
        ui = ui_base()
        ui.import_mesh, ui.import_as_skeletal, ui.import_animations = True, False, False
        ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
        d = ui.static_mesh_import_data
        d.combine_meshes = combine
        d.auto_generate_collision = False
        d.generate_lightmap_u_vs = False
        d.build_nanite = False
        d.remove_degenerates = False
        got = run(f, dest, ui)
        if not got:
            report["errors"].append(f)
        for p in got:
            m = unreal.load_asset(p)
            if isinstance(m, unreal.StaticMesh):
                if complex_collision:
                    bs = m.get_editor_property("body_setup")
                    if bs:
                        bs.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
                        eal.save_loaded_asset(m)
                b = m.get_bounds()
                report["meshes"][p] = [round(v, 1) for v in (b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z)]

def skeletal(folder, name, skeleton=None, anims=True):
    dest = f"{ROOT}/{folder}"
    if eal.does_asset_exist(f"{dest}/{name}"):
        return unreal.load_asset(f"{dest}/{name}").skeleton
    ui = ui_base()
    ui.import_mesh, ui.import_as_skeletal, ui.import_animations = True, True, anims
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_SKELETAL_MESH
    ui.create_physics_asset = False
    if skeleton:
        ui.skeleton = skeleton
    got = run(f"{SRC}/{folder}/{name}.fbx", dest, ui)
    if not got:
        report["errors"].append(name)
    mesh = unreal.load_asset(f"{dest}/{name}")
    if mesh:
        b = mesh.get_bounds()
        report["meshes"][f"{dest}/{name}"] = [round(v, 1) for v in (b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z)]
    return mesh.skeleton if mesh else None

def tidy_anims(folder, name):
    """<whatever>_<action> -> A_<name>_<action>, so C++ can load by path."""
    dest = f"{ROOT}/{folder}"
    for p in eal.list_assets(dest, recursive=False):
        a = unreal.load_asset(p)
        if not isinstance(a, unreal.AnimSequence):
            continue
        an = a.get_name()
        if an.startswith("A_") or not an.startswith(name + "_"):
            continue
        action = an[len(name) + 1:]
        for junk in ("Anim_", "Armature_", "Armature|", name + "_"):
            action = action.replace(junk, "")
        new = f"{dest}/A_{name}_{action}"
        if eal.rename_asset(p.split(".")[0], new):
            report["anims"].setdefault(name, []).append(action)

static("chunks", combine=True, complex_collision=True)
for folder in ("props", "fx", "scenery", "buildings", "track", "terrain"):
    static(folder, complex_collision=folder in ("props", "buildings"))

skel = skeletal("characters", "SK_Bandit")
tidy_anims("characters", "SK_Bandit")
for n in ("SK_Heavy", "SK_Boss"):
    skeletal("characters", n)
    tidy_anims("characters", n)
for n in ("SK_Deputy", "SK_Dynamiter", "SK_Gunslinger", "SK_Rifleman"):
    skeletal("characters", n, skeleton=skel, anims=False)
hskel = None
for f in sorted(glob.glob(f"{SRC}/horses/SK_*.fbx")):
    n = os.path.splitext(os.path.basename(f))[0]
    if hskel is None:
        hskel = skeletal("horses", n)
        tidy_anims("horses", n)
    else:
        skeletal("horses", n, skeleton=hskel, anims=False)
for folder in ("train", "setpieces"):
    for f in sorted(glob.glob(f"{SRC}/{folder}/SK_*.fbx")):
        n = os.path.splitext(os.path.basename(f))[0]
        skeletal(folder, n)
        tidy_anims(folder, n)
for folder, n in (("fx", "SK_PlayerRevolver"), ("props", "SK_Gatling")):
    skeletal(folder, n)
    tidy_anims(folder, n)

eal.save_directory(ROOT, only_if_is_dirty=True, recursive=True)
out = os.path.join(unreal.Paths.project_saved_dir(), "ironhorse_import.json")
with open(out, "w") as fh:
    json.dump(report, fh, indent=1)
unreal.log(f"IRONHORSE IMPORT DONE {len(report['meshes'])} meshes, errors: {report['errors']}")
