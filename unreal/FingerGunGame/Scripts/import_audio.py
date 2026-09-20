import glob, os
import unreal
SRC = os.path.join(os.path.dirname(os.path.abspath(__file__)), "audio_src")
tasks = []
for f in sorted(glob.glob(SRC + "/*.wav")):
    t = unreal.AssetImportTask()
    t.filename, t.destination_path = f, "/Game/IronHorse/audio"
    t.automated, t.save, t.replace_existing = True, True, True
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
for name in ("train_loop", "music_day", "music_night", "music_boss"):
    loop = unreal.load_asset("/Game/IronHorse/audio/" + name)
    if loop:
        loop.set_editor_property("looping", True)
        unreal.EditorAssetLibrary.save_loaded_asset(loop)
unreal.log("IRONHORSE AUDIO DONE")
