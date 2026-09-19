# IRON HORSE (working title)

A first-person finger-gun train shootout. Red Dead vibe. Single player, PvE, built in Unreal.
You stand on the roof of a moving steam train. Bandits ride up beside you, climb onto the cars ahead, and fire from a second train. Your hand is the gun. Your body is the dodge. A webcam does all the sensing. No controller, no multiplayer. An optional Arduino glove (one switch under the thumb, a buzzer, an LED) adds a physical trigger and a buzz on every shot.

Tracks: **Press Start** (main target), **No Wrapper** (the tracker is the hard part), **Cold Start** (only if we are honestly eligible).

---

## 1. How it plays

| You do this | The game does this |
|---|---|
| Make a finger gun and point at the screen | Crosshair follows your hand |
| Drop your thumb, kick your hand up like recoil, or press the glove's switch | Fire. However you pretend to shoot, it shoots. The glove buzzes and flashes |
| Slap the bottom of your gun hand with your other hand | Reload (six rounds, like slamming a magazine home) |
| Lean or sidestep | Your view shifts, bullets miss |
| Duck | Drop under tunnels, low bridges, and gatling fire |
| Raise your other hand, palm open (when the meter is full) | Focus: time slows, sweep your finger over bandits to mark them, fire once to shoot them all |
| Drop your hand to your hip | Holster (used for the final quick-draw) |

Design rules:
- **Nobody explains anything.** Every prompt is a pictogram. Every menu item is something you shoot.
- **Every enemy shot is dodgeable.** Bandit raises gun, glint + rising sound for ~0.7s, then a bright slow tracer (~0.7s flight) aimed at where your head *was*. Move and it misses with a whiz.
- **Max 2 bandits shooting at once** (attack tokens). No undodgeable crossfire.
- **Player shots are instant (hitscan) with aim assist.** Webcam aim is noisy, so snap to any bandit within ~2 degrees. Generous beats accurate.
- **3 hits and you are down.** First hit shoots your hat off (you see it fly away). Second is a red vignette. Third ends the run.

## 2. The run (about 3 minutes)

0. **Station, train parked.** "MAKE A FINGER GUN" pictogram. Shoot 4 bottles on a crate: this is calibration *and* the tutorial. There are 7 targets and 6 rounds, so the gun runs dry and the slap-to-reload pictogram appears. Shoot the station bell to depart.
1. **First obstacle, no enemies.** A low water-tower spout swings at head height. "DUCK" pictogram. Teaches ducking safely.
2. **Riders (0:20-1:00).** Bandits gallop up on both sides. Slow telegraphed shots. Teaches leaning.
3. **Boarders (1:00-1:40).** Bandits climb onto the car ahead and pop out from behind crates. Then a tunnel: duck for the entrance, fight in the dark by muzzle flash and lantern light.
4. **Second train (1:40-2:30).** A bandit train pulls alongside. Shooters in windows and on roofs. Dynamite throwers: shoot the stick in the air or lean away. Gatling flatcar sweeps fire you must duck under.
5. **Showdown (2:30-3:00).** The boss lands on your car. Sound drops to wind and heartbeat. "HOLSTER". Whistle blows. Draw and fire. Your draw time in ms goes on the result screen.
6. **Wanted-poster result screen.** Score, accuracy, dodges, draw time, rank from Greenhorn to Legend. Shoot "RIDE AGAIN" to restart. The booth never needs a keyboard.

Cut order if behind: gatling, dynamite, Focus, second train, showdown. **Never cut:** station tutorial, riders, boarders, one duck obstacle, result screen.

## 3. The Red Dead look, cheaply

- Golden hour. Low warm sun, long shadows, warm height fog, dust in the air.
- Post-process: film grain, vignette, slight desaturation, warm LUT, thin letterbox bars, motion blur on the ground.
- Steam and smoke from the locomotive (Niagara). Camera sway and rumble from the train.
- Ragdolls. A shot bandit tumbling off a moving train is free comedy and free juice.
- Minimal HUD: revolver cylinder for ammo, hats for health, a small Focus meter.
- Sound carries it: train chug as the rhythm bed, harmonica or twangy guitar, distinct player vs bandit gunshots, bullet whiz panned left/right, ricochets, a reload clack on the slap.
- **The train never moves.** It sits at the origin. Ground, track, poles, cacti, and mesas scroll backward on a loop. Riders and the second train move relative to you.

## 4. Architecture

```
webcam -> tracker (Python, MediaPipe landmarks + our signal processing)
       -> OSC over UDP, port 7000
       -> Unreal: BP_TrackerInput component -> game
Unreal -> OSC port 7001 -> tracker (calibration, recenter)
```

- **Glove (optional):** Arduino on USB serial to the tracker, not to Unreal. Firmware in `arduino/finger_gun_glove/`, protocol in `tracker/README.md`. A switch press arrives in Unreal as an ordinary `/fg/fire`, so the game needs no changes. The tracker buzzes the glove on every shot and reload it detects.
- Tracker is Python, not a browser tab: a background browser tab gets throttled when Unreal has focus. The tracker runs on CPU, so Unreal keeps the GPU.
- Unreal uses the built-in **OSC plugin**. Blueprint only, no C++ needed.
- The tracker can run on the same machine (`127.0.0.1`) or on a second laptop (`--host <unreal machine ip>`) over a phone hotspot if the Unreal laptop is struggling.
- **The game is always playable with mouse and keyboard.** `BP_TrackerInput` has a mouse mode. Lohith never needs a camera to build the game.

### OSC contract (the seam between Akhil and the Unreal side)

All arguments are float32. Coordinates are normalized screen space, (0,0) top-left, (1,1) bottom-right.

Tracker to game, port 7000:

`/fg/state` every camera frame (~30Hz)

| # | Name | Range | Meaning |
|---|---|---|---|
| 0 | aimX | 0..1 | Crosshair X |
| 1 | aimY | 0..1 | Crosshair Y |
| 2 | aimValid | 0/1 | A raised hand is tracked. Hide crosshair when 0 |
| 3 | gunPose | 0/1 | Hand is in finger-gun shape (title screen) |
| 4 | holstered | 0/1 | Gun hand is down at the hip |
| 5 | lean | -1..1 | Positive = player moved to their right. Move camera right |
| 6 | duck | 0..1 | 1 = fully ducked |
| 7 | bodySpeed | 0..1 | How fast the player is moving |
| 8 | tracking | 0/1 | A person is in frame. Show "step into frame" when 0 for >1s |
| 9 | offHandOpen | 0/1 | Other hand raised with open palm (Focus) |

`/fg/fire` `[aimX, aimY]` one per shot. Aim is already rewound to where the player was pointing *before* the trigger motion. Use this aim, not the latest state.
`/fg/reload` `[1.0]` one per slap.

Game to tracker, port 7001 (optional, tracker has sane defaults without it):

`/fg/calib/begin` start calibration, reset stance baseline.
`/fg/calib/target` `[sx, sy]` the player is about to shoot a target at this screen position. Tracker pairs it with the next fire. During calibration the game counts *any* `/fg/fire` as a hit on the current bottle.
**Hide the crosshair during calibration.** The player should point at the bottle naturally, not steer a cursor onto it. The crosshair appears after the last bottle, already lined up with how they point.
`/fg/recenter` current position becomes neutral lean and standing height, **and wherever the player is pointing becomes the middle of the screen**. Send it when you know they are pointing at the centre (for example the moment they shoot a START sign placed mid-screen). The tracker also does this by itself each time a hand comes up, and the bottle calibration refines it.

Mouse mode mapping (Unreal side and `fake_tracker.py`): mouse = aim, LMB = fire, R = reload, A/D = lean, S = duck, H = holster, F = Focus.

## 5. Who does what

### AKHIL: the tracker (`tracker/`, Python)

**Status: v1 of all of this is in [tracker/](tracker/), see [tracker/README.md](tracker/README.md).** 33 tests pass, tuned once against a recorded 72 s live session (see the README for what it changed), ~24 ms/frame on an M3. Not yet done: steps 9 and 10, and every threshold is still a first guess until it is tuned on real hands with the checklist in the README.

Found on the way: `mediapipe 1.0.1` is unusable on macOS (CPU path aborts at load, GPU path leaks ~10 MB per frame and dies after a couple of minutes), so `requirements.txt` pins `0.10.21`. Do not upgrade it. Lean and duck come from the shoulders rather than the head, because the gun hand covers the face from the camera's view. The neutral stance is learned the first time the player stands still, so nobody has to stand dead center.

1. **Hour 1, unblocks everyone:** push `fake_tracker.py` (mouse and keys in an OpenCV window, sends the exact OSC above) and `osc_monitor.py` (prints whatever arrives on 7000). Jason builds against the fake.
2. **Landmarks.** Webcam 1280x720, HandLandmarker (2 hands) + PoseLandmarker lite, debug window with skeleton and FPS. Test standing 4-5 ft back. Target 25+ FPS.
3. **Aim.** How far the fingertip has moved relative to the chest, in real metres (each scaled by its own depth), at one fixed sensitivity (36 cm of travel crosses the screen), around a centre learned from where the player first points. Steady and a little heavy on purpose. Leaning does not drag it, it works seated close or standing far, and it cannot get stuck off screen. Use the knuckle more than the fingertip: a finger pointed straight at the camera is the worst case for hand tracking. One Euro filter. Default mapping works with no calibration. Send `/fg/state`.
4. **Trigger.** Thumb drop (thumb-tip distance normalized by palm size, hysteresis + velocity threshold) and recoil flick (upward velocity spike from rest). Shared 250ms cooldown. Aim history ring buffer, rewind to gesture onset. Send `/fg/fire`. Pass mark: 18 of 20 deliberate shots register, zero fire while just aiming.
5. **Reload slap.** A slap looks like a recoil kick, so: a kick from either hand while the hands are together is a reload, a kick with them apart is a shot. A fast approach seen by the hand model also counts. Send `/fg/reload`.
6. **Body.** Lean from chest X, duck from chest Y against an auto-captured standing baseline, holster from wrist vs hip line, open-palm detect on the off hand.
7. **Calibration handshake** on port 7001: collect 4 (raw aim, screen target) pairs, least-squares fit per axis with a clamped gain (natural pointing at a small screen would otherwise make the crosshair twitchy). `/fg/recenter`.
8. **Tuning tools.** Record landmarks to a file and replay them through the detectors, so thresholds get tuned without standing at the camera. Live bars in the debug window showing each feature against its threshold.
9. Test on the actual demo machine (Windows) and in remote `--host` mode. Tune on strangers, not on us.
10. Write the one-page No Wrapper explainer with a diagram.

### LOHITH: the game (Unreal)

Build everything against `BP_TrackerInput` in mouse mode. Until Jason hands it over, stub the same variables on the pawn with keyboard input.

1. UE5 project, Blueprint-first. First-person pawn fixed to a train roof, no locomotion. Camera offset = `lean` x ~1m sideways, `duck` x ~0.8m down, interpolated. Head hit-capsule follows the camera.
2. Scrolling world: train at origin, looping ground/track/prop segments moving backward. Camera sway.
3. Shooting: on `OnFire(aim)` deproject the screen point, line trace with aim assist, damage, tracer, muzzle flash, impact. Six rounds, `OnReload` refills.
4. Base bandit: spawn, move to a slot, telegraph, fire a slow projectile at the head position sampled at fire time, repeat. Ragdoll on death. Attack-token manager caps simultaneous shooters at 2.
5. Player damage: projectile overlaps head capsule = lose a hat. Near miss = whiz + dodge score.
6. **Checkpoint: riders + boarders + one duck obstacle + result screen = shippable game.** Get here before anything else.
7. Bandit variants by slot and reskin: rider, boarder, window shooter, dynamite thrower, gatling.
8. Wave director: one data table that is the 3-minute timeline.
9. Station tutorial flow with Jason's calibration calls. Showdown finale using `holstered`. Focus mode using `offHandOpen` (global time dilation ~0.25, mark on crosshair overlap, chain-fire on next shot).
10. Packaged build on the demo machine by feature freeze.

### JASON: Unreal input bridge, integration, demo rig

1. Enable the OSC plugin. Build `BP_TrackerInput` (actor component): OSC server on 7000 (keep the server in a variable or it gets garbage collected), parse the three addresses, expose variables plus `OnFire(Vector2D)` and `OnReload` dispatchers. Test against Akhil's `fake_tracker.py`.
2. Mouse mode inside the same component, toggled by a key. The game must never depend on the camera to run.
3. Interpolate the 30Hz state up to frame rate so the crosshair and camera are smooth. Crosshair widget, hidden when `aimValid` is 0. "Step into frame" overlay when `tracking` is 0 for over a second.
4. Game-to-tracker OSC client on 7001 for the calibration and recenter messages.
5. Repo hygiene for Unreal: `.gitignore` for `Binaries/ Intermediate/ Saved/ DerivedDataCache/`, third-party asset packs kept out of git and shared as a zip. `.uasset` files cannot be merged, so one person edits a given Blueprint at a time.
6. **The rig:** camera on top of the screen, biggest screen we can borrow, desk lamp on the player, tape mark on the floor at ~4.5 ft, one-click launcher that starts tracker + game, hotspot ready if the tracker runs on a second laptop.
7. Latency check: film hand and screen together in phone slow-mo, count frames from thumb drop to muzzle flash. Target under 120ms.
8. Backup gameplay video in case the demo machine dies.

### JERRY: look, sound, story, submission

1. **Hour 1:** moodboard and one-page style guide. Palette, golden-hour reference shots, fonts (free western slabs such as Rye, Sancreek, Special Elite).
2. Source third-party assets and log every one in `CREDITS.md` with link and license as you go (the rules require it): steam train, desert kit (Megascans), characters and animations (Mixamo: aim, fire, hit, death, climb).
3. **Decide by hour 3: horses.** A free horse with a gallop animation is the hardest asset on the list. Fallback is bandits on a rail handcar or a wagon. Tell Lohith early.
4. Original work, which Press Start rewards, so keep a list: title logo, HUD (cylinder, hats, Focus meter), wanted-poster result screen, tutorial pictograms (finger gun, slap reload, lean, duck, open palm), rank names.
5. Audio: train loop, player and bandit gunshots, whiz, ricochet, reload clack, station bell, whistle, hit grunt, music. Team-recorded voice barks ("He's on the roof!", "DRAW!").
6. Color grade and post-process pass with Lohith.
7. Submission: Devpost text per track, screenshots, 60-90s video, 2-minute pitch, booth sign that just says MAKE A FINGER GUN.

## 6. Timeline (hours from now)

- **0-1:** OSC contract agreed (this doc). Fake tracker pushed. UE project created. Moodboard.
- **1-6:** Akhil: landmarks, aim, trigger. Jason: `BP_TrackerInput` working against the fake. Lohith: pawn, scrolling world, shooting, first bandit. Jerry: train, desert, characters in engine.
- **6:** **First integration.** Real webcam moves the crosshair and fires in Unreal. Expect it to feel bad. That is why it is at hour 6 and not hour 16.
- **6-12:** Akhil: reload, lean, duck, tuning. Lohith: riders, boarders, damage, duck obstacle, result screen. Jason: calibration flow, smoothing, rig. Jerry: HUD, audio, grade.
- **12:** **Shippable checkpoint** (Lohith step 6) played end to end with the webcam.
- **12-18:** Second train, dynamite, showdown, Focus, in cut order. Stranger playtests every hour. Fix what confuses them.
- **18-22:** Feature freeze. Bugs, packaged build, latency check, backup video.
- **22-24:** Devpost, pitch rehearsal, booth setup.

## 7. What we tell No Wrapper judges

1. **A camera-only trigger that feels instant.** Two fused gestures, hysteresis and velocity thresholds on scale-normalized landmarks, and the aim is rewound to the onset of the gesture so pulling the trigger does not throw the shot.
2. **Dodging that does not wreck your aim.** Aim is measured relative to your shoulders, filtered with a One Euro filter, and calibrated with a least-squares fit from four shots.
3. **Reload detection that survives tracking failure.** Two overlapping hands break the hand model, so contact is inferred from approach velocity plus the disappearance itself.
4. **A real-time bridge into a game engine.** 30Hz OSC state, event messages, latency budget measured on camera.
5. Game side: attack-token director that guarantees every shot is dodgeable.

Be upfront: MediaPipe's landmark models are off-the-shelf neural nets, not language models. Everything above them is ours. Confirm with an organizer early.

## 8. Risks

| Risk | Plan |
|---|---|
| Hand tracking is poor at 4-5 ft or when pointing at the camera | Hour-2 test. Aim uses hand position, not finger direction. Fall back to pose wrist. Stand closer. |
| Dim venue lighting | Bring a lamp. It matters more than any code. |
| Unreal + tracker fight for one laptop | Run the tracker on a second laptop over a hotspot. |
| Asset rabbit hole | Jerry time-boxes sourcing. Gray boxes are fine until hour 12. |
| Blueprint merge conflicts | One owner per `.uasset`. |
| Flick trigger fires while re-aiming | It can be disabled with a flag. Thumb drop alone is enough. |
| Venue WiFi blocks laptop-to-laptop | Phone hotspot or ethernet. |

## 9. Check before submitting

- Cold Start needs 75% first-time hackers and nobody with professional SWE experience. Be honest about it.
- Confirm one project may enter multiple tracks.
- `CREDITS.md` is complete.
