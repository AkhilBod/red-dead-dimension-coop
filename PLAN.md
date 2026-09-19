# High Noon: first-person finger-gun duel over LAN

## Context

SteelHacks project targeting three tracks at once: No Wrapper (no LLM, something hard and explainable), Press Start (one mechanic, playable with zero explanation), Cold Start (beginner, learning story).
Idea: two players, two laptops, each sees the other in first person 20 ft away on a western street. Your hand is the gun. Your body is the dodge. A minimal glove adds only what a camera cannot: an instant trigger and physical recoil.
Directory `/Users/akhil/Desktop/fingerGUn` is empty. Greenfield.

Design rule (keeps it from becoming "a controller"): **one input on the glove, the trigger switch. Everything else is your body.** No joystick, no menu buttons.

## Game design

**Flow (under 30s to first shot, no text instructions needed)**
1. "MAKE A FINGER GUN" with a hand silhouette. Pose detected, crosshair appears.
2. Calibrate = tutorial: shoot 4 bottles in the screen corners. Also joins the lobby.
3. "HOLSTER" (drop hand to hip). Both holstered, wind, silence, random 2-5s, bell.
4. Draw, shoot, dodge. One body hit wins the round. Best of 3. Draw time shown in ms.

**Mechanics**
- Aim: index fingertip position *relative to your head*, so leaning to dodge does not drag your aim.
- Trigger: thumb presses micro switch on the side of the middle finger (the natural "hammer drop" motion). Fallback with no glove: thumb-drop gesture, or spacebar.
- Dodge: lean/sidestep moves your avatar (amplified ~2.5x). Your first-person camera shifts with your head (parallax window effect), so dodging feels physical.
- Duck: drop behind your barrel. Safe, but you cannot shoot. Barrel splinters after 3 hits (stops turtling).
- Bullets are visible tracers, ~0.5s travel over 20 ft. Dodgeable but hard. Whiz audio panned L/R on near miss.
- Steady hands shoot straight: crosshair bloom grows with body speed. Move to live, stand still to hit.
- Six shots. Reload by pointing the gun at the sky.
- Drawing before the bell = foul, gun jams 1s.
- Opponent is a paper-puppet cowboy driven by their live pose landmarks. You read your friend's real body language. Only landmarks cross the network, no video.
- Solo bot bandit (random reaction 350-600ms, aim error, same bullet rules) so one judge can play alone. Also the dev test harness.
- AR mirror inset: small view of your own webcam with muzzle flash on your fingertip and a hat on your head.

**Staging:** laptops back to back on a table so players physically face each other, each ~5 ft from their own screen. Works equally with laptops 20 ft apart. No code difference.

## Hardware (per glove, need two)

From the hardware table list:
- Arduino (Uno-class) strapped to forearm, USB to laptop. Need a ~3m USB cable or extension.
- Micro Switch = trigger.
- Servo Motor = "hammer" on the back of the hand. Snaps on fire. Tactile kick and visible to spectators. No motor driver needed.
- Buzzer = click on fire, rattle when hit.
- LED on fingertip if the table has any (220 ohm resistor). Muzzle flash.
- Stretch only: 3-Axis Accelerometer for ms-accurate draw detection via gravity vector.

Serial protocol, 115200 baud. Glove to PC: `T` on debounced press. PC to glove: `F` fire (servo + buzzer + LED), `H` hit taken (long buzz), `N` near miss (tick). Non-blocking servo timing with `millis()`.

If only one Arduino is available, player two uses the thumb-drop gesture trigger.

## Tech

Browser. Vite + TypeScript, Three.js (flat planes in 3D, paper-diorama look), `@mediapipe/tasks-vision` (HandLandmarker numHands 1 + PoseLandmarker lite, VIDEO mode, 1280x720), Web Serial, Web Audio (synthesized gunshot/bell/whiz, no asset files), `ws` relay server run with `tsx`.
Model `.task` files and wasm bundled in `public/models/` so venue WiFi is irrelevant.

```
client/src/
  main.ts                 app state machine: title, calibrate, lobby, duel, result
  tracking/tracker.ts     camera + MediaPipe loop (hands every frame, pose alternate frames if slow)
  tracking/oneEuro.ts     One Euro filter
  tracking/aim.ts         head-relative aim, 4-point calibration, aim ring buffer (rewind ~60ms on fire)
  tracking/gestures.ts    finger-gun pose, holster line, point-up reload, thumb-drop fallback
  tracking/body.ts        lean, duck, body speed -> bloom
  hw/glove.ts             Web Serial in/out, auto-reconnect via getPorts()
  net/protocol.ts         message types (shared with server)
  net/client.ts           WebSocket, clock sync
  game/duel.ts            round state machine
  game/bullets.ts         bullet sim, defender-side hit test
  game/bot.ts             solo bandit
  render/scene.ts         street, barrel, head-coupled camera, shake, slow-mo on final hit
  render/puppet.ts        opponent puppet from remote landmarks
  render/hud.ts           crosshair+bloom, ammo, round pips, AR mirror inset
  audio/sfx.ts            synthesized SFX
server/relay.ts           rooms of 2, clock-sync pings, schedules bell at a future server timestamp, scores
firmware/glove/glove.ino
```

**Networking rules**
- Each laptop runs the client on its own `localhost` (camera and Web Serial need a secure context; `http://192.168.x.x` is not one). Only the WebSocket points at the host IP: `localhost:5173?host=<ip>`.
- Clock sync: NTP-style offset from ping round trips. Bell is scheduled at a server time ~1s ahead so both screens ring together.
- Hits are defender-authoritative: shooter sends `{tFire, origin, dir}`, defender simulates the bullet against their own current body. If you saw yourself dodge, you dodged.
- Pose landmarks sent at ~20Hz, interpolated on the other side.
- Venue WiFi may block peer traffic. Fallback: phone hotspot. Relay URL is a query param so a cloud relay can be swapped in.

## Build order (each step is playable)

0. **Spike, first 1-2h:** hand + pose tracking together at 5 ft. Measure FPS and fingertip stability. If hand landmarks are too weak at distance, aim falls back to pose wrist relative to shoulder. Decide here.
1. Aim + crosshair + One Euro + calibration, shooting bottles. Debug mode `?debug=mouse` (mouse aim, click fire, A/D lean, S duck) for fast iteration.
2. Glove v1: switch to `T`, `F` back to servo/buzzer. Aim rewind on fire.
3. Duel vs bot: holster, bell, foul, bullets, lean, duck, barrel, bloom, reload.
4. Relay server, clock sync, remote puppet, defender-side hits. Test with two windows on one machine, one in mouse debug.
5. Juice: SFX, shake, slow-mo final hit, result screen with draw ms, AR inset.
6. Second glove, staging rehearsal, fallbacks verified.

Cut line if behind: drop barrel/duck first, then AR inset, then second glove. Never cut the bot or mouse debug.

## What to tell No Wrapper judges (the hard parts)

1. Head-relative aim mapping + One Euro filter + calibration, so dodging and aiming decouple.
2. Camera/button sensor fusion: 1ms switch timestamp, aim rewound ~60ms to cancel trigger flinch.
3. Fair duel over a network: clock sync, synchronized bell, defender-authoritative hits with bullet travel time.
4. Pose landmarks retargeted to a puppet, ~1KB/frame instead of video.
5. Firmware: debounce, non-blocking actuation, two-way serial protocol.

Be upfront: MediaPipe landmark models are off-the-shelf neural nets, not language models. Confirm with an organizer. Everything on top is ours.

## Check before committing

- Cold Start eligibility: 75% first-time hackers, no professional SWE experience.
- One project allowed in multiple tracks.
- Two Arduinos, two servos, two switches, long USB cables available.
- Chrome or Edge on both laptops (Web Serial).

## Verification

- `npm run dev` + `?debug=mouse&bot=1`: full round playable with mouse and keyboard.
- Same with camera: FPS overlay >= 25 with both models running, crosshair jitter under ~10px when hand is still at 5 ft.
- Glove: press switch, shot fires and servo kicks within the same frame; latency overlay shows switch-to-shot time.
- `npm run server` on one laptop, both clients connected: clock offset overlay stable within ~10ms, bells ring together (film both screens with a phone), dodged bullets never register as hits on the defender's screen.
- Pull the glove USB mid-round: game falls back to thumb-drop trigger without reload.
- Cold walk-up test: someone who has not seen it gets to their first shot in under 30s with no verbal help.
