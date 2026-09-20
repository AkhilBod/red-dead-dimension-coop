"""Content/IronHorse/Data/assets.json: every asset under /Game/IronHorse with its class.
The game finds animations by name and preloads everything; in a packaged build the asset registry comes back empty
for these folders, so it reads this list instead. Re-run after importing art or audio."""
import json, os
import unreal
reg = unreal.AssetRegistryHelpers.get_asset_registry()
reg.scan_paths_synchronous(["/Game/IronHorse"], True)
rows = []
for d in reg.get_assets_by_path("/Game/IronHorse", recursive=True):
    rows.append({"path": str(d.package_name), "name": str(d.asset_name), "class": str(d.asset_class_path.asset_name)})
rows.sort(key=lambda r: r["path"])
out = os.path.join(unreal.Paths.project_content_dir(), "IronHorse/Data/assets.json")
with open(out, "w") as f:
    json.dump({"assets": rows}, f, indent=0)
unreal.log(f"IRONHORSE MANIFEST {len(rows)} assets")
