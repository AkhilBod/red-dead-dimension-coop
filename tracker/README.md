# Tracker

Webcam in, OSC out. The game (Unreal) listens on UDP **7000**. The tracker listens on **7001**.
The message contract is in [PLAN.md](../PLAN.md) section 4 and in [protocol.py](protocol.py).

## Setup (once)

Python 3.10 to 3.12.

```bash
cd tracker
python -m venv .venv
```

```bash
source .venv/bin/activate        # Windows: .venv\Scripts\activate
```

```bash
pip install -r requirements.txt
```

## Run it

```bash
python run.py
```

macOS asks for camera access for your terminal the first time. Allow it, then run again.
Stand 4 to 5 ft back with your shoulders in frame. Light your face and hands.

| Flag | Use |
|---|---|
| `--host 10.0.0.5` | Unreal is on another machine |
| `--camera 1` | pick another webcam |
| `--pose-every 2` | body model every 2nd frame, for slow laptops |
| `--no-window` | no preview, slightly faster |
| `--record` | save landmarks to `recordings/` |
| `--replay file.jsonl` | run the detectors over a recording, no camera |
| `--set key=value` | override anything in [config.py](config.py), repeatable |

Keys in the window: **Q** quit, **C** calibrate (shoot the 4 red targets), **N** recenter stance, **F** flick trigger on/off, **R** record on/off.

## Reading the window

- Green skeleton = the gun hand. Orange = the other hand. Dark red "ignored" = a detection judged not to be your hand (background object, spectator, duplicate). Yellow line = shoulders.
- Green crosshair = where the game thinks you aim (the window stands in for the game screen). Red X = where a shot landed.
- `thumb` bar: fires when it falls past the red tick (30% below its recent peak). Green tick = the minimum "thumb up" level. Gray bar = frozen because the hand is moving, or waiting for the thumb to come back up.
- `kick m` bar: how far the fingertip has risen above the wrist, in metres. Past the red tick = a recoil kick.
- `hand v` bar: hand speed in m/s. Past the red tick the thumb trigger is frozen.
- `hands dx` bar: sideways gap between your two hands. Inside the yellow ticks and yellow = hands together, so a kick now is a **reload**, not a shot.
- Flags on top: TRACK (body seen), GUN (a hand is up as the gun), AIM (crosshair live), HOLSTER, OPEN (other hand open palm).

## First session checklist

1. 25+ fps in the corner. If not: `--pose-every 2`, close other apps.
2. TRACK and AIM light up. Crosshair follows your hand and reaches all four corners comfortably. Too twitchy or too sluggish: change `aim_span_x` (metres of real hand travel per screen width, bigger = less sensitive), or press **C**.
3. Fire 20 thumb shots at one spot: pop the thumb up, drop it. Want 18+ to register, none while just aiming. Missing shots: lower `thumb_drop_frac`. Firing by itself: raise it.
4. Fire 10 recoil shots: kick the fingertip up. Small kicks not registering: lower `flick_rise_m` (0.04). It costs false shots when you re-aim upward fast, which is why it is 0.05.
5. Red X should land where you were aiming *before* the trigger motion, not where your hand ended up.
6. Sweep your aim around fast. No shots should fire. If the recoil trigger misfires, press **F** to turn it off.
7. Slap the bottom of your gun hand. One RELOAD, no FIRE, and the crosshair must not jump to the far side afterwards.
8. Step left and right, duck. `lean` and `duck` follow and return to zero when you stand normally.

## Tuning without standing at the camera

Press **R**, do 20 shots, some fast re-aims, 5 reloads, press **R** again. Then:

```bash
python run.py --replay recordings/<file>.jsonl --no-window --set thumb_drop_frac=0.35
```

It prints every FIRE and RELOAD with a timestamp and a count at the end. Change a value, rerun, compare.

## Build the Unreal side with no camera

```bash
python fake_tracker.py
```

Mouse = aim, click = fire, R = reload, A/D = lean, S = duck, H = holster, F = off-hand open.
It sends exactly what the real tracker sends. `--auto` runs scripted motion with no window. `--host <ip>` targets another machine.

```bash
python osc_monitor.py
```

Pretends to be Unreal and prints every message. `--calib` also drives a 4-target calibration like the game will. Close Unreal first, only one program can own port 7000.

## Tests

```bash
python -m unittest discover -s tests
```

Synthetic landmarks, no camera. They check the logic (one shot per pull, aim rewind, no shots from re-aiming or slaps, lean does not move the aim, calibration, stance learning). They do not prove the thresholds fit real hands. Only the checklist above does.

## How it works

| File | Job |
|---|---|
| [landmarks.py](landmarks.py) | MediaPipe hand + body models on two threads, record/replay |
| [hand_features.py](hand_features.py) | the hand's image scale (how we get real centimetres), open palm, thumb feature |
| [body.py](body.py) | chest depth from shoulder width, chest anchor, lean, duck, stance baseline |
| [aim.py](aim.py) | hand position relative to chest in metres -> screen, calibration, aim history |
| [trigger.py](trigger.py) | thumb drop and recoil kick, each reporting when the gesture *began* |
| [reload.py](reload.py) | a kick while the hands are together is a reload, a kick with them apart is a shot |
| [pipeline.py](pipeline.py) | ties it together, no camera or network inside so it can be tested |
| [one_euro.py](one_euro.py), [windows.py](windows.py) | speed-adaptive low-pass filter, time-windowed medians and percentiles |
| [config.py](config.py) | every threshold, with units |

## What the first recorded session taught us

Thresholds marked `[rec]` in [config.py](config.py) come from replaying a real 72 s session, and each of these has a regression test:

- A background object was detected as a 0.99-confidence hand and stole the aim whenever the real hand dropped out. Hands now have to be plausibly the player's: not farther away than their chest, and either on one of their arms or clearly held out in front.
- Seated at a laptop the hand is ~2.6x nearer the camera than the chest, so chest-scaled aim was 2.6x too twitchy and pinned to the screen edges. Hand travel is now measured in real metres using the hand's own apparent size.
- During reload slaps the gun lock slid onto the other hand and mirrored the aim. The model's Left/Right labels flip with hand orientation, so identity now comes from which arm of the body model the hand is on.
- The thumb landmarks are garbage while the hand moves, which fired stray shots during recoil. The thumb trigger skips those frames and looks for a relative drop from its own recent peak.
- Real recoil kicks took ~0.25 s to rise, not a sharp 0.1 s snap.
- A slap looks exactly like a recoil kick. The difference is where the other hand is.

**Do not upgrade mediapipe.** `requirements.txt` pins `0.10.21` on purpose. On macOS, `1.0.1` aborts at load on the CPU path, and its GPU path leaks about 10 MB per frame until the process dies a couple of minutes in (this killed a live test). `0.10.21` on CPU holds ~24 ms per frame at a flat ~300 MB on an M3.
