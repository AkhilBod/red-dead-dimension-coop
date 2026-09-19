# High Noon: first-person finger-gun duel over LAN (camera only, no hardware)

## Context

SteelHacks project targeting three tracks at once: No Wrapper (no LLM, something hard and explainable), Press Start (one mechanic, playable with zero explanation), Cold Start (beginner, learning story).
Two players, two laptops, each sees the other in first person 20 ft away on a western street. Your hand is the gun. Your body is the dodge. **No hardware: a webcam is the only sensor.** Any two laptops with Chrome can play.
Repo: https://github.com/AkhilBod/fingerGunGame (only `PLAN.md` so far). Team of 4.

## Trigger without a button (the new hard problem)

However you pretend to shoot, it shoots:
- **Thumb drop** (hammer falls). Thumb-tip to index-PIP distance, normalized by palm size. Fires on a fast drop below a low threshold, re-arms above a high threshold (hysteresis).
- **Recoil flick** (hand kicks up and returns within ~200ms). Gross motion, so it still works when the hand is small in frame.
- Either one fires. Shared ~250ms cooldown.
- **Aim rewind:** both gestures move the fingertip, so the shot uses the aim from the moment the gesture *started* (ring buffer of aim history), not where the finger ended up. This is what makes camera-only shooting feel accurate.
- Feedback replaces haptics: camera kick, muzzle flash, loud synthesized shot, flash in the AR mirror inset.
- Reload = finger pointed at the sky, held ~300ms (sustained, so it never collides with the flick).
- Stretch/fun mode: shout "BANG" to fire (mic onset detection, only while gun pose is held).

## MVP

One laptop. Gray boxes for art. A box bandit that shoots back. You can dodge.

**Hour 0, Akhil alone, push before anyone else codes:** skeleton repo.
- Vite + TS single root package, Three.js, `npm run dev` works.
- `InputSource` interface: `aim {x,y}|null`, `gunPose`, `holstered`, `lean -1..1`, `duck 0..1`, `bodySpeed`, `landmarks`, `onFire`, `onReload`.
- `MouseInput` implementation: mouse aim, click fire, A/D lean, S duck, H holster, R reload. Enabled by `?debug=mouse`.
- `OpponentSource` interface (lean, duck, landmarks, shots). Stub that stands still.
- Asset manifest with fixed filenames. Missing file = colored placeholder plane.
- `src/tuning.ts` holding every gameplay constant (bullet speed, dodge gain, bot reaction, bloom).

After that push, all four people work in parallel against those seams.

Milestones to a playable MVP:
- **M1** `CameraInput`: webcam 1280x720, HandLandmarker + PoseLandmarker lite, debug overlay, FPS. Spike at 4-5 ft: is fingertip aim stable, does thumb drop register. If not, aim falls back to pose wrist relative to shoulder and the flick becomes the main trigger.
- **M2** Head-relative aim + One Euro + crosshair + 4-bottle calibration + both trigger gestures + aim rewind.
- **M3** Duel loop vs bot on the gray-box street (built in parallel by P2 on `MouseInput`), then swap in `CameraInput`.

## Team split (4 people, no hardware)

Each person owns their folders. Merge to `main` every couple of hours.

| Who | Owns | First task after skeleton | Done when |
|---|---|---|---|
| **P1 Akhil: camera input** | `src/tracking/`, `src/main.ts` | M1 spike at real standing distance | Aim, both triggers, holster, reload, lean, duck all work from the webcam |
| **P2: game rules + bot** | `src/game/`, `src/tuning.ts` | Duel state machine in `?debug=mouse`: holster, random wait, bell, foul, result | Full best-of-3 vs bot with bullets, dodge, ammo, reload, bloom, barrel |
| **P3: art + audio + feel** | `public/assets/`, `src/render/`, `src/audio/` | One style frame (street + cowboy), then the asset list | Every placeholder replaced, puppet rigged, SFX, shake, slow-mo, AR mirror inset |
| **P4: networking, then demo** | `server/`, `src/net/` | `ws` relay echoing between two tabs | Two laptops duel. Then stranger playtests, tuning, Devpost, pitch, backup video |

If someone does not code:
- P2 not coding: Akhil builds M3 after M2. P2 owns `tuning.ts` numbers, bot difficulty, and playtesting.
- P3 not coding: P3 stays purely on assets. Akhil or P4 wires `src/render/`.
- P4 not coding: Akhil takes networking after M3. P4 goes straight to playtests, Devpost, pitch, video, and helps P3 with assets.

Worst case (only Akhil codes), order is: skeleton, M1, M2, M3, network, juice. Two people on art (scene vs characters/HUD), one on playtest and submission.

**Timeline (24h)**
- H0-1: Skeleton pushed. Others install Node + Chrome, clone, run it, P3 starts the style frame.
- H1-8: M1, M2 (P1). Duel loop vs bot in mouse mode (P2). First asset pass (P3). Clock sync + state relay in two tabs (P4).
- H8-14: Integrate. Camera input into the duel, assets into the scene, network opponent replaces bot.
- H14-20: Duck + barrel, bloom, AR inset, puppet polish, juice, stranger playtests.
- H20-24: Feature freeze. Bugs, demo rehearsal, Devpost, backup video.

Cut order if behind: barrel/duck, AR inset, BANG mode. Never cut the bot or mouse debug.

## Bring

2 laptops with webcams, Chrome or Edge. Boxes to raise screens to chest height. A desk lamp (tracking needs light). Phone hotspot as network fallback. Nothing from the hardware table.

## Asset list for P3 (PNG, transparent, exact filenames)

Style: paper cutout. Thick outline, flat colors, white sticker border. Drawing on paper, photographing, and removing the background is fastest and counts as own art.

- Scene: `bg_sky` 2048x1024, `ground` 1024 tile, `building_l1..l3`, `building_r1..r3` ~1024 tall, `barrel`, `barrel_broken`, `bottle`, `bottle_broken`, `tumbleweed`
- Puppet, one PNG per part, joint at the top edge: `p_head`, `p_hat` (separate, it flies off), `p_torso`, `p_upperarm`, `p_forearm`, `p_hand_gun`, `p_thigh`, `p_shin`
- HUD/FX: `crosshair`, `bullet_icon`, `title_logo`, `hand_silhouette`, `draw_banner`, `foul_banner`, `win`, `lose`, `muzzle_flash`, `smoke_puff`, `hat_inset`
- Audio is synthesized in Web Audio (gunshot, bell, whiz, wind). Optional: one music loop, credited

## Full game design (post-MVP target)

- Flow: "MAKE A FINGER GUN" silhouette, shoot 4 bottles (calibration = tutorial = lobby join), "HOLSTER", bell, duel. One body hit wins the round. Best of 3. Under 30s to first shot.
- Aim is fingertip relative to head, so dodging does not drag aim.
- Lean/sidestep dodges (amplified ~2.5x), camera moves with your head. Duck behind barrel: safe, cannot shoot, barrel breaks after 3 hits.
- Tracer bullets, ~0.5s travel over 20 ft. Whiz panned L/R on near miss.
- Crosshair bloom grows with body speed. Six shots. Early draw = 1s jam.
- Opponent is a paper puppet driven by their live pose landmarks (~20Hz, no video).
- AR mirror inset: your webcam with muzzle flash on the fingertip and a hat.
- Staging: laptops back to back so players face each other, or 20 ft apart. No code difference.

## Tech

Vite + TypeScript, Three.js, `@mediapipe/tasks-vision` (models + wasm bundled in `public/models/`), Web Audio, `ws` relay run with `tsx`.

```
src/main.ts                 app state machine
src/tuning.ts               all gameplay constants
src/input/                  InputSource.ts, MouseInput.ts
src/tracking/               CameraInput.ts, tracker.ts, oneEuro.ts, aim.ts, trigger.ts, gestures.ts, body.ts
src/game/                   duel.ts, bullets.ts, bot.ts, OpponentSource.ts
src/net/                    protocol.ts, client.ts (clock sync), NetOpponent.ts
src/render/                 scene.ts, puppet.ts, hud.ts, mirror.ts, assets.ts (manifest + placeholders)
src/audio/sfx.ts
server/relay.ts             rooms of 2, clock pings, bell scheduled at future server time, scores
```

Networking rules:
- Each laptop runs the client on its own `localhost` (camera needs a secure context). Only the WebSocket points at the host: `localhost:5173?host=<ip>`.
- NTP-style clock offset. Bell scheduled ~1s ahead in server time.
- Defender-authoritative hits: shooter sends `{tFire, origin, dir}`, defender simulates against their own body.
- Venue WiFi may block peers. Fallback: phone hotspot. Relay URL is a query param.

## No Wrapper talking points

1. A camera-only trigger that feels instant: normalized landmark features, hysteresis + velocity thresholds, two gestures fused, aim rewound to gesture onset to cancel flinch.
2. Head-relative aim + One Euro filter + calibration: dodge and aim decouple.
3. Fair network duel: clock sync, synchronized bell, defender-side hits with bullet travel.
4. Pose retargeted to a puppet at ~1KB/frame instead of video.

Be upfront that MediaPipe landmark models are off-the-shelf neural nets, not language models. Confirm with an organizer.

## Check before committing to tracks

- Cold Start: 75% first-time hackers, no professional SWE experience.
- One project allowed in multiple tracks.

## Execution steps on approval

1. Copy this plan over `PLAN.md`, commit, push to `main` so the team sees the new split and asset list.
2. Build the Hour 0 skeleton, verify `?debug=mouse` runs in the browser pane, commit and push so teammates can clone.
3. Continue with M1, M2, then M3 if P2 has not covered it. Commit and push after each milestone.

## Verification

- `npm run dev` with `?debug=mouse&bot=1`: a full round is playable with mouse and keyboard.
- With camera: FPS overlay >= 25 with both models, crosshair jitter under ~10px with a still hand at standing distance.
- Calibration: after the 4 bottles, pointing at each screen corner puts the crosshair there.
- Trigger: 20 deliberate shots at a bottle, at least 18 register, zero fire while just aiming. Shots land where the crosshair was before the thumb moved.
- Bot fires tracers, leaning makes them miss, standing still gets you hit, result screen shows draw ms.
- Later: two laptops ring the bell together (film both screens). Dodged bullets never count on the defender's screen.
- Cold walk-up: a stranger reaches their first shot in under 30s with no verbal help.
