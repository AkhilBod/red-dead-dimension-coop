# Iron Horse: what was added to this project

Open `Content/Levels/IronHorse` (now the startup map) and press Play, or run `play.sh` from the tracker repo. No tracker running = mouse mode (LMB fire, R reload, A/D lean, S duck, H holster).

Everything is C++ in `Source/FingerGunGame/FG*.cpp`, built on the existing classes. The level is empty: `AFGIronHorseGameMode` (set as the level's game mode override) spawns the lot.

| File | What |
|---|---|
| `FGTrainPlayer` | subclass of `AFingerGunPlayerCharacter`. Feeds `SetAimNormalized` / `SetBodyInput` / `Fire` / `Reload` from the tracker, adds the revolver viewmodel and three hats of health |
| `FGBandit` | subclass of `ABanditEnemyBase`. Riders, boarders, train shooters, dynamiter, boss. 0.7 s telegraph, attack tokens (max 2 shooting) |
| `FGTrackerInput` | OSC over UDP 7000 (`/fg/state`, `/fg/fire`, `/fg/reload`), commands back on 7001, camera preview JPEG on 7002, mouse fallback. Plain sockets, no OSC plugin |
| `FGWorldStreamer` | the chunk system: reads `Content/IronHorse/Data/chunks.json`, chains chunks by joint type, moves the world under the fixed train, exposes duck / dark / side_track events |
| `FGTrain` | consists placed on the track pose (own train and the bandit train on the side track) |
| `FGIronHorseGameMode` | the run: bottles (tracker calibration), cans, bell, duck, riders, boarders + tunnel, second train, showdown, result poster. Sky, post-process, steam, sfx |
| `FGHud` | canvas HUD: crosshair, cylinder, hats, prompts, camera preview, poster |
| `FGFx`, `FGTarget`, `FGAssets` | short-lived meshes, shootable props, load-by-name helpers |

Art: `Scripts/import_art.py` imports `art/export/*.fbx` from the tracker repo into `/Game/IronHorse` (headless, see the docstring). `Scripts/make_audio.py` + `import_audio.py` make the placeholder sounds. `Scripts/make_level.py` makes the level.

Test switches on the command line: `-FGAuto` (plays itself), `-FGGod`, `-FGShots=4` (screenshot every 4 s to `Saved/Screenshots`), `-FGSkip=95` (jump to 95 s into the ride).

Build with the editor closed. If any Unreal window is open, the build writes `-0001` hot-reload copies and the game keeps loading the old module.

Changed existing files: `FingerGunGame.Build.cs` (socket, image, json modules), `Config/DefaultEngine.ini` (startup and default map).

## This copy (in the tracker repo)

`Content/Characters` (the 126 MB template mannequins) is left out. The Iron Horse level does not use it; the First Person template Blueprints will warn about it if opened. First use on a new machine: open `FingerGunGame.uproject` and let it rebuild the module. Art re-imports from `../../art/export` with `Scripts/import_art.py` if `Content/IronHorse` ever needs regenerating.
