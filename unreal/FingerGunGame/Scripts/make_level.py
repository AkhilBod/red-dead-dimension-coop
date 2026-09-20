"""Create /Game/Levels/IronHorse: an empty level whose game mode builds the whole game at runtime."""
import unreal
PATH = "/Game/Levels/IronHorse"
les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if unreal.EditorAssetLibrary.does_asset_exist(PATH):
    les.load_level(PATH)
else:
    les.new_level(PATH)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
ws = world.get_world_settings()
ws.set_editor_property("default_game_mode", unreal.load_class(None, "/Script/FingerGunGame.FGIronHorseGameMode"))
les.save_current_level()
unreal.log("IRONHORSE LEVEL SAVED " + str(ws.get_editor_property("default_game_mode")))
