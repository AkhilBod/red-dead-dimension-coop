# Red Dead Dimension: what to know for the presentation

For the system design, the Unreal patterns and the flow charts, see [ARCHITECTURE.md](ARCHITECTURE.md).

One page per question a judge can ask. Numbers here are measured, not guessed; where something is untested it says so.

## The pitch (15 seconds)

You stand on the roof of a moving steam train. Your hand is the gun, your body is the dodge, a webcam is the only controller. Make a finger gun and point: that is the crosshair. Drop your thumb: that is the trigger. Slap your gun hand: reload. Lean and duck for real to dodge bullets and tunnels. It never ends: every lap is faster and meaner until you lose your three hats.

Booth sign: **MAKE A FINGER GUN.** Nobody explains anything; the game does.

## How to run the demo

```bash
./play.sh
```

Starts the game, then the tracker in that terminal (the camera permission belongs to the terminal). No tracker = mouse mode, so the game is never dead on stage: LMB fire, R reload, A/D lean, S duck, H holster.

Keys during play: **Space** recentre (player moved, camera bumped), **P** switch aim model (travel from a learned centre, or crosshair on the fingertip).

Before you present:

- Light on the player's hands and face. It matters more than any code.
- Player about arm's length to 1.5 m from the camera, shoulders in frame.
- Launch once beforehand. The first launch on a machine builds every mesh (about two minutes on a black screen); after that it starts in under a second.
- Close other heavy apps. The tracker needs to hold 30 fps: when the game was starving it (25 fps, 268 gaps over 100 ms in ten minutes, measured from our own session recordings) the aim felt laggy.
- Have the backup gameplay video ready.

## What the player does

| You do | The game does |
|---|---|
| Finger gun, point | Crosshair follows, leans onto targets (aim magnetism) |
| Drop your thumb, or press the glove switch | Fire. The glove buzzes and flashes |
| Slap the gun hand from below with the other hand | Reload |
| Lean, sidestep | View shifts up to a metre, bullets miss |
| Duck | Under water-tower spouts, signal gantries, tunnel roofs. Standing up in a tunnel costs a hat every 1.5 s |
| Drop the hand to the hip | Holster, for the duel |

One lap: a town, riders on horseback, a trestle bridge, boarders climbing onto the boxcar ahead, a slot canyon, a tunnel, a bandit train alongside with riflemen and dynamite, then a quick-draw duel with the boss at sunset. Beat him and night falls, you get a new gun (revolver, shotgun, rifle, long Colt), and it goes round again harder. Speed climbs 26 to 40 m/s, enemies alive 2 to 4, enemy bullets arrive in 0.75 down to 0.5 s. Signal arms over half the roof have to be leaned round. Headshots win a hat back. Oil barrels take everyone near them.

Fairness rules that make it feel good: every enemy shot is telegraphed for 0.7 s with a red flash at the barrel and a closing ring on the HUD; one bandit may be shooting at once on the first lap, two after that; nobody shoots from off screen; bullets are aimed at where your head **was**, so moving always works.

## Architecture

```
webcam -> tracker (Python: MediaPipe hand + pose landmarks, then our signal processing)
       -> OSC over UDP 7000   /fg/state 30 Hz, /fg/fire, /fg/reload
       -> Unreal 5.8 (C++):   FGTrackerInput -> player, game mode
Unreal -> UDP 7001            /fg/recenter, /fg/aim_mode, calibration
tracker -> UDP 7002           camera preview as JPEG slices, shown in the HUD corner
Arduino glove (optional) -> USB serial -> tracker (never talks to Unreal)
```

- The tracker is a separate Python process on the CPU so the engine keeps the GPU. A browser tab would be throttled when the game has focus.
- OSC is parsed with plain UDP sockets in C++ (about 100 lines), no plugin.
- macOS refuses UDP datagrams over 9216 bytes, so the preview JPEG goes in 8 KB slices with a 8-byte header and is reassembled in the game.
- The game is C++ on top of Lohith's player and bandit classes. The level is empty: the game mode builds the world, trains, sky and enemies at runtime, so there is nothing to wire up in the editor.

## The hard parts (this is the No Wrapper story)

MediaPipe gives 21 hand points and 33 body points per frame. It is an off-the-shelf landmark model, not a language model, and it knows nothing about guns. Everything that turns wobbling points into a game that feels instant is ours. Say that plainly.

1. **Aiming without knowing where the finger points.** A finger pointed at the camera is foreshortened to nothing: every direction-based model we tried on recorded sessions landed tens of centimetres off and jittered 1 to 4 cm. Fingertip **position** is steady to about 2 mm. So aim is how far the fingertip has travelled in real metres (image distance divided by the hand's own image scale), minus how far the chest travelled, so dodging does not drag the crosshair. 46 cm of travel crosses the screen, seated close or standing far.
2. **A trigger that does not throw the shot.** Dropping the thumb jerks the hand. The trigger is a relative fall (30% below the thumb's own recent peak, two frames to confirm, ignored while the hand is moving because thumb landmarks are garbage then). The shot is fired from the aim **before** the gesture began: a ring buffer of aim history, rewound to the gesture's onset, median if the hand was still, a line fit if it was sweeping.
3. **Lag.** One Euro filter (min cutoff 1.0, beta 20) plus a 30 ms velocity lead that only switches on while the hand is moving. Measured against a zero-lag reference on recordings: trailing error while moving went from 4 to 5% of the screen down to about 1.5%. On the game side the crosshair chases small differences slowly and big ones fast, so rest shimmer disappears and real moves are not delayed, and it rides through 0.4 s hand dropouts instead of blinking.
4. **Reload that survives tracking failure.** Two overlapping hands break the hand model. Contact is inferred from approach speed, the hands' relative position, a kick from either hand, and the disappearance itself.
5. **Which hand is even the player's.** A background object once came back as a 0.99-confidence hand and stole the aim. Hands are rejected if they are farther than the player's chest or not on either arm of the body model. Left/right labels flip with hand orientation, so identity comes from the body model's arms.
6. **Body.** Lean and duck come from the shoulders (the gun hand hides the face), with the head's extra sideways travel added when the face is visible. Looking down at the screen tucks the head like a duck does, so the head barely counts for ducking: with it fully in, a seated player read as ducking 42% of the time instead of 15%.
7. **Forgiveness.** Webcam aim is noisy, so hits count within 7 degrees of the ray, the crosshair is magnetised toward targets, and the head box covers the hat.

How it was tuned: `play.sh` records every session as landmarks (no video). Problems are replayed offline through the real pipeline and thresholds changed against data. The first version passed 27 synthetic tests and was badly wrong live; recordings found what tests could not. Today: 55 automated tests, all passing.

## The world (the chunk system)

- 38 hand-generated 50 m chunks (flat, rocky, canyon, mesa, curves, S-bends, trestle gulch, deep canyon, tunnel, side track, landmarks), about 3k to 8k triangles each. All art is original, generated by our Blender Python scripts: 7 enemy types sharing one skeleton with 47 animations, horses, two train liveries, a town's worth of buildings and props.
- Each chunk end is one of four joint types. Chunk B may follow A when the joints match, which is measured to a 0.0 m ground gap at every legal pairing. Curves ease curvature to zero at both ends, so position, heading and curvature all match.
- **The train never moves.** The streamer lays chunks end to end in "chain space" and every frame moves the whole chain by the inverse of the track pose under the player, so curves swing the world round the train. Each car sits on its own point of the track, so the train bends. Chunks are dropped 70 m behind and laid 450 m ahead; the run is infinite.
- The director queues set pieces by name (a town street, a bridge, a canyon, a tunnel, the side track for the second train) and the streamer places them as soon as the joint rule allows. Chunk metadata carries events (duck here, dark here, trestle, side track) that drive prompts, lighting and enemy rules: riders rein in before a trestle, nobody spawns near a tunnel.
- The sun belongs to the landscape, so it swings with the mesas on a curve, sets behind the first boss, and night and day alternate after that.

## Performance

M3 MacBook Air, 1600x900: the project's defaults (Lumen, ray tracing, virtual shadow maps, real-time sky capture) gave 36 fps with 5 to 7 second freezes, because running from the editor binary builds each asset the first time it is used. Now every asset is preloaded and held, the expensive features are switched off at runtime, chunks carry no collision, unseen meshes do not animate: a locked 60 fps with no freezes (measured in autoplay, tracker not running). Tracker: about 24 ms per frame on the same machine.

## Hardware (optional glove)

Arduino: switch on pin 6, buzzer 7, LED 8. It talks serial to the tracker only. A press arrives in the game as an ordinary fire event, fired at the crosshair as it was at the click (a click has an exact time, so it is not rewound). The tracker buzzes the glove on every shot and reload. Everything works with no board attached.

## What is original, what is not

Original: all tracker signal processing, all game code, all 3D art and animation (procedural Blender scripts), all sound effects (synthesised in Python; three music loops exist too but are off by default, `-FGMusic` turns them on), the chunk system.

Not ours: MediaPipe hand and pose landmark models (Google), Unreal Engine 5.8, OpenCV, python-osc, NumPy/SciPy, Lohith's base player and bandit classes started from the UE first-person template. Much of the code was written with an AI coding assistant (Claude Code) directed by us; say so if asked, and check the track rules on it before claiming a track.

## Tracks

- **Press Start** (main): a complete, replayable game with original art, sound and music, playable by a stranger with no instructions.
- **No Wrapper**: the hard part is the layer between raw landmarks and a game that feels instant. Points 1 to 7 above.
- **Cold Start**: only if we honestly meet the eligibility rules. Check them.

## Likely questions

- *Why not point with the finger direction?* Tried it on recordings. Foreshortening makes it unusable; position is 10x steadier. There is a live switch (P) to a pointer-on-the-fingertip mode to show the difference.
- *Latency?* About one camera frame plus 24 ms of processing, with a 30 ms lead while moving. The game adds no queueing: UDP, newest packet wins.
- *What happens when tracking drops?* Crosshair holds 0.4 s, then hides. A hand gone 0.6 s starts again from screen centre. Space recentres. Mouse always works.
- *Does it work for anyone?* Aim scales by the hand's own image size and shoulder width, neutral stance is learned when the player first stands still. Tested mostly seated close; standing far is the demo case, check it on the day.
- *Why Unreal and Python rather than one program?* The engine keeps the GPU, the tracker keeps a steady CPU loop, and either side can be replaced: `fake_tracker.py` and mouse mode let the game and tracker be built separately.
- *How is difficulty scaled?* Speed, enemy count, shooters at once, fire rate and bullet speed all rise per lap and per minute to fixed caps. The 0.7 s telegraph never shrinks, so every shot stays dodgeable.

## Honest limits

- The latest round of changes (guns per lap, music, night laps, taller tunnel, boss-fight fix, head-assisted lean) compiled but had not been playtested when this was written.
- A standalone Mac app is built (`./play.sh --app`). No Windows build yet: `Scripts/package_windows.bat` must be run on a Windows machine with UE 5.8.
- Dim light and busy backgrounds hurt tracking. Bring a lamp.
- Dual wielding and a one-handed pump reload were built, tried and switched off; they are options in the tracker config.

## Who did what

Akhil: tracker. Lohith: Unreal base classes and project. Jason: glove hardware and firmware. Jerry: look, audio direction, submission. Fill in what each of you would say in one sentence before you go up.
