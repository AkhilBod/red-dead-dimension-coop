# Tracker

Webcam in, OSC out. The game (Unreal) listens on UDP **7000**. The tracker listens on **7001**. The small camera picture the game shows in its corner goes to **7002** as JPEG slices ([preview.py](preview.py), `--no-preview` turns it off).

Reload is the two-handed slap (`reload_gesture=slap`). `pump` = jerk the muzzle up with one hand instead, `both` allows either. `dual_wield` (off by default) makes a second hand in gun shape a second gun: `/fg/state2 [x, y, valid, firstGunOnRight]`, and `/fg/fire` gains a third arg, 0 or 1.

Two aim models, `--set aim_mode=finger` or **P** in the game: `travel` (default) measures how far the fingertip has moved from a learned centre, `finger` puts the crosshair where the fingertip is in the camera picture (`finger_gain` sets how much of the picture covers the screen). **Space** in the game sends `/fg/recenter`.

With the game: `../play.sh` from the repo root starts both. By hand: `.venv/bin/python run.py --no-window` (the venv, not conda's python).
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
| `--no-window` | no tuning window. **Run the demo machine like this** |
| `--fps 30` | camera frame rate to ask for. Default 60: fresher frames, less lag. A camera that cannot do it gives what it has, and the start-up line says what you got |
| `--pose-every 2` | cap the body model at every 2nd frame (it already backs off by itself on a busy machine) |
| `--record` | save landmarks to `recordings/` |
| `--replay file.jsonl` | run the detectors over a recording, no camera |
| `--set key=value` | override anything in [config.py](config.py), repeatable |

The window opens fullscreen on purpose: the crosshair is mapped to the whole screen, like the game's will be, so in a small window it would travel less than your finger points. `--windowed` or **W** changes that.

Keys: **Q** quit, **X** centre the crosshair on where you are pointing now, **[ ]** sensitivity down/up, **C** calibrate (shoot the 4 red targets), **N** recenter stance, **F** recoil trigger on/off (it starts off), **R** record on/off, **W** fullscreen on/off.

Sensitivity is one physical number: `aim_span_m`, the metres of fingertip travel that cross the screen (default 0.36). **]** makes it more sensitive, **[** less, and it prints the value to keep with `--set aim_span_m=...`.

## The Arduino glove (optional)

A switch under the thumb, a buzzer and an LED. The **switch is a third trigger** next to the two gestures, and the **buzzer + LED go off on every shot and reload**, including the ones detected from the camera. With no board plugged in the tracker runs exactly as before.

1. Wire it as in Jason's sketch: switch on **6** (reads HIGH when pressed), buzzer on **7**, LED on **8**.
2. In the Arduino IDE open [arduino/finger_gun_glove/finger_gun_glove.ino](../arduino/finger_gun_glove/finger_gun_glove.ino), upload it, then **close the Serial Monitor** (only one program can hold the port).
3. `python run.py`. It finds the board by itself and prints `[glove] connected on ...`. The top line of the window shows `glove on <port>`. If it picks the wrong port: `--arduino /dev/cu.usbmodem1101` (Windows: `--arduino COM5`). `--arduino off` skips it.

| Glove to PC | | PC to glove | |
|---|---|---|---|
| `READY` | once at boot | `F` | a shot fired: 100 ms buzz + flash |
| `T` | trigger pressed | `R` | reload: two short clicks |
| `FG1` | answer to `?` | `H` | got hit: 300 ms buzz (nothing sends this yet) |

115200 baud. Pressing the switch buzzes on the board itself, with no round trip, so it still works with no PC attached. The tracker sends `F` only for shots that did *not* come from the switch, so nothing buzzes twice. The switch sits where the thumb lands, so pressing it is also a thumb-drop gesture: the tracker counts that as one shot. Like the gestures, a press jolts the hand, so the shot lands where the aim was held just before it.

Jason's original [switch_led_buzz.ino](../arduino/switch_led_buzz/switch_led_buzz.ino) is untouched, as the standalone wiring test.

## Reading the window

- Green skeleton = the gun hand. Orange = the other hand. Dark red "ignored" = a detection judged not to be your hand (background object, spectator, duplicate). Yellow line = shoulders.
- Green crosshair = where the game thinks you aim (the window stands in for the game screen). Red X = where a shot landed.
- `thumb` bar: fires when it falls past the red tick (30% below its recent peak). Green tick = the minimum "thumb up" level. Gray bar = frozen because the hand is moving, or waiting for the thumb to come back up.
- `kick m` bar: how far the fingertip has risen above the wrist, in metres. Past the red tick = a recoil kick.
- `hand v` bar: hand speed in m/s. Past the red tick the thumb trigger is frozen.
- `hands dx` bar: sideways gap between your two hands. Inside the yellow ticks and yellow = hands together, so a kick now is a **reload**, not a shot.
- Flags on top: TRACK (body seen), GUN (a hand is up as the gun), AIM (crosshair live), HOLSTER, OPEN (other hand open palm).

## First session checklist

1. The fps number top-left is white at 25+, yellow under 25, red under 20. It should sit at the camera's 30. Under 20 nothing else on this list means anything: close other apps.
2. Raise your finger gun pointing at the middle of the screen. TRACK and AIM light up and the crosshair appears **in the middle**, wherever your hand is. It should feel steady and a little heavy, like a gun, not like a mouse: about 36 cm of fingertip travel crosses the screen. Want more: **]**. Less: **[**. Flick into a corner and back: the crosshair must return to where it was. Hold your hand past an edge for a second or two and it pulls the mapping along, so you can never lose it. **X** re-centres on where you point now.
3. Fire 20 thumb shots at one spot: pop the thumb up, drop it. Want 18+ to register, none while just aiming. Missing shots: lower `thumb_drop_frac`. Firing by itself: raise it.
4. Only if you turn the recoil trigger on (**F**, off by default because it fired while re-aiming): fire 10 recoil shots, kick the fingertip up. Small kicks not registering: lower `flick_rise_m` (0.04). It costs false shots when you re-aim upward fast, which is why it is 0.05.
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
| [landmarks.py](landmarks.py) | MediaPipe hand + body models on two threads, the cheap second-hand search, record/replay |
| [hand_features.py](hand_features.py) | the hand's image scale (how we get real centimetres), open palm, thumb feature |
| [body.py](body.py) | chest depth from shoulder width, chest anchor, lean, duck, stance baseline |
| [aim.py](aim.py) | fingertip position relative to the chest -> screen: geometric gain, learned centre, edge push, calibration, aim history |
| [trigger.py](trigger.py) | thumb drop and recoil kick, each reporting when the gesture *began* |
| [reload.py](reload.py) | a kick while the hands are together is a reload, a kick with them apart is a shot |
| [glove.py](glove.py) | serial link to the Arduino glove: trigger presses in, buzzer/LED codes out, reconnects by itself |
| [run.py](run.py) | camera thread, tracker thread, window on the main thread, OSC, glove, keys |
| [pipeline.py](pipeline.py) | ties it together, no camera or network inside so it can be tested |
| [one_euro.py](one_euro.py), [windows.py](windows.py) | speed-adaptive low-pass filter, time-windowed medians and percentiles |
| [config.py](config.py) | every threshold, with units |

## Frame rate

Every time-based rule in the detectors assumes about 30 frames a second, so this matters more than any threshold. Measured on a busy M3 Air, per frame:

| | before | now |
|---|---|---|
| Hand model with one hand in view | 35 ms | **15 ms** |
| Waiting on the body model | up to 25 ms | 0 to 4 ms |
| Showing the tuning window | 14 ms, blocking | 0 (own thread) |
| **Tracker frame rate, window open** | **13.6 fps** | **30 fps** (camera limit), ~25 ms camera to OSC |

What changed:
- Asked for 2 hands while 1 is in view, MediaPipe re-runs its expensive palm *search* on every frame looking for the other. Now a 1-hand model runs normally and a 2-hand model takes a look every 8 frames, taking over only while two hands are really there. A second hand is still picked up within about a quarter second.
- The body model never holds a frame up. If it has not finished, the frame goes out with the newest body available.
- The tracker runs on its own thread. The window (which macOS forces onto the main thread) draws whatever is newest and cannot slow the game's input down.
- The GPU delegate was tested again on the pinned MediaPipe: 9 ms a frame, but it still leaks (236 MB to 1.5 GB in 240 frames), so it stays off.

## What the first recorded session taught us

Thresholds marked `[rec]` in [config.py](config.py) come from replaying a real 72 s session, and each of these has a regression test:

- A background object was detected as a 0.99-confidence hand and stole the aim whenever the real hand dropped out. Hands now have to be plausibly the player's: not farther away than their chest, and either on one of their arms or clearly held out in front.
- Seated at a laptop the hand is ~2.6x nearer the camera than the chest, so chest-scaled aim was 2.6x too twitchy and pinned to the screen edges. Hand travel is now measured in real metres using the hand's own apparent size.
- During reload slaps the gun lock slid onto the other hand and mirrored the aim. The model's Left/Right labels flip with hand orientation, so identity now comes from which arm of the body model the hand is on.
- The thumb landmarks are garbage while the hand moves, which fired stray shots during recoil. The thumb trigger skips those frames and looks for a relative drop from its own recent peak.
- Real recoil kicks took ~0.25 s to rise, not a sharp 0.1 s snap.
- A slap looks exactly like a recoil kick. The difference is where the other hand is.
- Looking for a second hand only every few frames (the frame rate fix) made quick slaps easy to miss. The body model now cues it: the moment the other wrist comes near the gun hand, or both wrists are up, both hands are tracked. And with the hands together a gentler kick (3.5 cm, not a shot's 5) counts as a reload.
- "The crosshair is not where my finger points." True, and it cannot be computed: a ray along the finger, wrist-to-tip and elbow-to-tip rays were all tried on the recordings and land tens of centimetres off screen with 1-4 cm of jitter, because a finger pointed at the camera is foreshortened to nothing. Fingertip *position* is steady to ~2 mm. So aim is how far the fingertip has moved in real metres, around a centre learned from where the player first points.
- A geometric gain (~21 cm per screen, changing as the arm extended) was "too sensitive" and "gets messed up in one corner". Two causes. The sensitivity is now one fixed number. And the aim used to divide the hand's *position in the frame* by an estimated scale, so near the frame's corners a few percent of scale wobble became centimetres of error: it now measures *travel* from a reference point. The screen edges used to re-centre the mapping instantly on every touch: now only a hand *held* past an edge pulls it, at a steady pace, so a flick into a corner and back lands where it started.
- Recoil shots landed a median 7.5% of the screen (worst 17%) off target, because a kick is only recognisable a few frames in and the aim was taken from there. Shots now use the median aim over the 0.15 s *before* the gesture began.

**Do not upgrade mediapipe.** `requirements.txt` pins `0.10.21` on purpose. On macOS, `1.0.1` aborts at load on the CPU path, and its GPU path leaks about 10 MB per frame until the process dies a couple of minutes in (this killed a live test). `0.10.21` on CPU holds ~24 ms per frame at a flat ~300 MB on an M3.
