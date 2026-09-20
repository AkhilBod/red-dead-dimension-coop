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

Keys while playing: **Space** recentres (stance and aim: use it when the player or the camera has moved), **P** switches the tracker between aim-from-a-learned-centre and crosshair-on-the-fingertip. `Scripts/fix_materials.py` makes every imported material two-sided (the scenery is open shells).

Test switches on the command line: `-FGAuto` (plays itself), `-FGGod`, `-FGShots=4` (screenshot every 4 s to `Saved/Screenshots`), `-FGSkip=95` (jump to 95 s into the ride), `-FGPerf` (fps and worst frame to the log every 2 s).

Performance: the game mode preloads and builds every asset at startup (the first launch on a machine takes about two minutes on a black screen while meshes build; afterwards under a second) and switches Lumen, ray tracing, virtual shadow maps and real-time sky capture off at runtime. M3 Air, 1600x900: 36 fps with multi-second freezes before, a locked 60 after.

Build with the editor closed. If any Unreal window is open, the build writes `-0001` hot-reload copies and the game keeps loading the old module.

Changed existing files: `FingerGunGame.Build.cs` (socket, image, json modules), `Config/DefaultEngine.ini` (startup and default map).
