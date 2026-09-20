"""M_FG_Glow: unlit, translucent, a colour and an opacity. Explosions, smoke, steam and dust use it so you can see through them."""
import unreal
PATH, NAME = "/Game/IronHorse/fx", "M_FG_Glow"
mel = unreal.MaterialEditingLibrary
if unreal.EditorAssetLibrary.does_asset_exist(f"{PATH}/{NAME}"):
    unreal.EditorAssetLibrary.delete_asset(f"{PATH}/{NAME}")
mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, PATH, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("two_sided", True)
color = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -400, 0)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.8, 0.6, 1.0))
opacity = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -400, 250)
opacity.set_editor_property("parameter_name", "Opacity")
opacity.set_editor_property("default_value", 0.4)
mel.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)
mel.recompile_material(mat)
unreal.EditorAssetLibrary.save_loaded_asset(mat)
unreal.log("IRONHORSE GLOW MATERIAL SAVED")
