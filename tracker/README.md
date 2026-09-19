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

- Green skeleton = the gun hand. Orange = the other hand. Yellow line = shoulders.
- Green crosshair = where the game thinks you aim (the window stands in for the game screen). Red X = where a shot landed.
- `thumb` bar: red tick = fire level, green tick = re-arm level. Drop your thumb and the bar must fall past the red tick.
- `flick` bar: red tick = how hard a recoil kick must be.
- `slap d` bar: distance from your other hand to the gun wrist. Turns red while shots are vetoed around a reload.
- Flags on top: TRACK (body seen), GUN (finger-gun shape), AIM (crosshair live), HOLSTER, OPEN (other hand open palm).

## First session checklist

1. 25+ fps in the corner. If not: `--pose-every 2`, close other apps.
2. TRACK and AIM light up. Crosshair follows your hand and reaches all four corners without stretching. If not, press **C**, or change `aim_span_x` (bigger = less sensitive).
3. Fire 20 thumb-drop shots at one spot. Want 18+ to register, none while just aiming. Bar not reaching the red tick: raise `thumb_fire_frac`. Firing by itself: lower it.
4. Red X should land where you were aiming *before* the trigger motion, not where your hand ended up.
5. Sweep your aim around fast. No shots should fire. If the flick misfires, raise `flick_rise` or press **F** to turn it off.
6. Slap the bottom of your gun hand from below. One RELOAD, no FIRE.
7. Step left and right, duck. `lean` and `duck` bars follow and return to zero when you stand normally.

## Tuning without standing at the camera

Press **R**, do 20 shots, some fast re-aims, 5 reloads, press **R** again. Then:

```bash
python run.py --replay recordings/<file>.jsonl --no-window --set thumb_fire_frac=0.55
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
| [body.py](body.py) | scale from shoulder width, chest anchor, lean, duck, stance baseline |
| [aim.py](aim.py) | hand position relative to chest -> screen, calibration, aim history |
| [trigger.py](trigger.py) | thumb drop and recoil flick, each reporting when the gesture *began* |
| [reload.py](reload.py) | slap detection that survives the hand model losing a hand on contact |
| [pipeline.py](pipeline.py) | ties it together, no camera or network inside so it can be tested |
| [one_euro.py](one_euro.py) | speed-adaptive low-pass filter |
| [config.py](config.py) | every threshold, with units |

**Do not upgrade mediapipe.** `requirements.txt` pins `0.10.21` on purpose. On macOS, `1.0.1` aborts at load on the CPU path, and its GPU path leaks about 10 MB per frame until the process dies a couple of minutes in (this killed a live test). `0.10.21` on CPU holds ~24 ms per frame at a flat ~300 MB on an M3.
