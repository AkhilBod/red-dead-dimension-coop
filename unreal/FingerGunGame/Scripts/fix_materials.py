"""Every imported material two-sided. The low-poly scenery is open shells (mesas with no back, rocks with no
underside), so with backface culling you see straight through them from the wrong side and they look hollow."""
import unreal
eal = unreal.EditorAssetLibrary
n = 0
for path in eal.list_assets("/Game/IronHorse", recursive=True):
    asset = unreal.load_asset(path)
    if isinstance(asset, unreal.Material) and not asset.get_editor_property("two_sided"):
        asset.set_editor_property("two_sided", True)
        unreal.MaterialEditingLibrary.recompile_material(asset)
        eal.save_loaded_asset(asset)
        n += 1
    elif isinstance(asset, unreal.MaterialInstanceConstant):
        o = asset.get_editor_property("base_property_overrides")
        if not (o.get_editor_property("override_two_sided") and o.get_editor_property("two_sided")):
            o.set_editor_property("override_two_sided", True)
            o.set_editor_property("two_sided", True)
            asset.set_editor_property("base_property_overrides", o)
            unreal.MaterialEditingLibrary.update_material_instance(asset)
            eal.save_loaded_asset(asset)
            n += 1
unreal.log(f"IRONHORSE MATERIALS two-sided: {n}")
