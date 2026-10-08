# How Red Dead Dimension is built

The design, the Unreal patterns it uses, and the flow of events, with diagrams (they render on GitHub). Source is in `unreal/FingerGunGame/Source/FingerGunGame/` and `tracker/`.

## 1. The whole system

```mermaid
flowchart LR
    cam[Webcam 30 fps] --> lm[MediaPipe<br/>hand + pose landmarks]
    subgraph tracker [Tracker: Python process, CPU]
        lm --> hands[Which hands are<br/>the player's]
        hands --> aim[Aim: fingertip travel<br/>One Euro + lead]
        hands --> trig[Thumb trigger<br/>+ aim rewind]
        hands --> rel[Slap reload]
        lm --> body[Body: lean, duck,<br/>holster, head assist]
    end
    glove[Arduino glove<br/>switch, buzzer, LED] <-->|USB serial| tracker
    aim -->|/fg/state 30 Hz| udp7000((UDP 7000))
    body --> udp7000
    trig -->|/fg/fire x y| udp7000
    rel -->|/fg/reload| udp7000
    tracker -->|JPEG slices| udp7002((UDP 7002))
    subgraph game [Unreal 5.8: C++]
        udp7000 --> input[UFGTrackerInput]
        udp7002 --> input
        input --> player[AFGTrainPlayer]
        input --> hud[AFGHud]
        player --> gm[AFGIronHorseGameMode]
        gm --> world[AFGWorldStreamer]
        gm --> bandits[AFGBandit x N]
        gm --> trains[AFGTrain x 2]
    end
    input -->|/fg/recenter, /fg/aim_mode| udp7001((UDP 7001)) --> tracker
```

Two processes, three UDP ports, one contract (`tracker/protocol.py`, PLAN.md section 4). Either side can be swapped out: `fake_tracker.py` stands in for the tracker, mouse mode stands in for it inside the game, `osc_monitor.py` stands in for the game.

## 2. Classes

```mermaid
classDiagram
    class ACharacter
    class AFingerGunPlayerCharacter {
        Lohith's base
        SetAimNormalized()
        SetBodyInput(lean, height)
        Fire() hitscan, ammo, cooldown
        Reload()
    }
    class AFGTrainPlayer {
        UFGTrackerInput Tracker
        USpringArmComponent CameraArm
        Revolver / LongGun viewmodel
        Hats, weapon per lap
        HandleFire(aim) HandleReload()
    }
    class ABanditEnemyBase {
        Lohith's base
        TakeDamage() Die()
        MaxHealth CurrentHealth
    }
    class AFGBandit {
        Kind: Rider Boarder TrainShooter Dynamiter Boss
        State machine
        Warning() red barrel flash
        Anchor: frame it rides in
    }
    class UFGTrackerInput {
        UDP sockets, OSC parser
        State (smoothed)
        OnFire OnReload delegates
        CameraTexture
        mouse fallback
    }
    class AFGIronHorseGameMode {
        phases, laps, stages
        difficulty curve
        attack tokens
        ResolvePlayerShot() aim assist
        sky, music, FX
    }
    class AFGWorldStreamer {
        chunks.json
        chain of placed chunks
        SetDistance() TrackWorld()
        MetresTo(event)
    }
    class AFGTrain { Build() Place() per-car track pose }
    class AFGTarget { bottles cans bell barrels dynamite }
    class AFGFx { short-lived mesh, glow fade }
    class AFGHud { canvas HUD }
    ACharacter <|-- AFingerGunPlayerCharacter
    AFingerGunPlayerCharacter <|-- AFGTrainPlayer
    ABanditEnemyBase <|-- AFGBandit
    AFGTrainPlayer *-- UFGTrackerInput
    AFGIronHorseGameMode o-- AFGTrainPlayer
    AFGIronHorseGameMode o-- AFGWorldStreamer
    AFGIronHorseGameMode o-- AFGTrain
    AFGIronHorseGameMode o-- AFGBandit
    AFGIronHorseGameMode o-- AFGTarget
    AFGBandit ..> AFGIronHorseGameMode : asks for a token, reports death
    AFGHud ..> AFGIronHorseGameMode : reads state
```

## 3. Unreal patterns used, and why

| Pattern | Where | Why |
|---|---|---|
| **Gameplay Framework roles** | `AGameModeBase` owns rules and the run; `APawn`/`ACharacter` is the player; `AHUD` draws; actors are things in the world | The engine's own separation of "rules", "body" and "screen". The HUD only reads the game mode, it never changes it |
| **Inheritance over the teammate's classes** | `AFGTrainPlayer : AFingerGunPlayerCharacter`, `AFGBandit : ABanditEnemyBase` | Lohith's ammo, cooldown, hitscan, health and death stay the single source of truth. Ours adds behaviour without editing his files, so two people can work without merge fights |
| **Component composition** | `UFGTrackerInput` (actor component) on the player; spring arm, camera, skeletal and static mesh components | Input is a reusable part, not baked into the pawn. The same component would drive any pawn |
| **Observer (delegates)** | `OnFire`, `OnReload` multicast delegates from the input component; the player subscribes with `AddUObject` | The input layer does not know who listens. Mouse, tracker and the autoplay test all raise the same events |
| **State machines** | Game: `EFGPhase` and `EFGStage`. Bandit: `EFGBanditState`. Showdown: steps 0 to 6 | Every behaviour is "what state am I in, what ends it". Easy to reason about on no sleep |
| **Director / spawner** | `TickRide` decides what to spawn and which set pieces to queue, from lap, stage time and difficulty | One place holds the pacing. Difficulty is five small functions (`TargetSpeed`, `MaxAlive`, `TokenLimit`, `ShotFlight`, `FireDelayScale`) |
| **Token (semaphore)** | `RequestAttackToken` / `ReleaseAttackToken` | At most 2 (later 3) bandits may be mid-attack, which guarantees no undodgeable crossfire |
| **Data-driven content** | `chunks.json`: joints, centrelines, events, rigs, weights. Weapons are a table of structs | New chunks and guns are data, not code. The art pipeline writes the JSON, the game reads it |
| **Streaming with a moving origin** | `AFGWorldStreamer`: the train stays at the origin, the world is transformed under it | No floating-point drift however far you ride, and all gameplay happens in one fixed frame: "12 m ahead on the boxcar roof" is a constant |
| **Anchor frames (strategy as a function)** | `FFGBanditSpec::Anchor`, `AFGTarget::Anchor`: a `TFunction<FTransform()>` giving the frame an object lives in | The same bandit code rides our boxcar, the other train, or open ground. Fixed a real bug: props in world coordinates slid across the roof on curves |
| **Asset loading by name** | `FGAssets`: `LoadObject` by path, animations found through the Asset Registry by suffix | No Blueprint wiring. Re-importing art never breaks references. Preloaded and held at start so nothing builds mid-run |
| **Object lifetime via `UPROPERTY`** | Every held actor, component, texture and preloaded asset is a `UPROPERTY` `TObjectPtr` | The garbage collector must see references, or it frees them. (Dropped assets being rebuilt was one cause of the early freezes) |
| **Fire-and-forget FX actors** | `AFGFx::Spawn(...)`: mesh, lifetime, velocity, optional light and translucent fade | No particle system to author. A muzzle flash, tracer, smoke puff and flying hat are the same 60-line actor |
| **Runtime configuration by console variable** | Renderer features switched off in `BeginPlay` | The project settings stay the artist's; the game takes what it needs for 60 fps without touching them |
| **Polling, non-blocking sockets** | UDP read in the component's tick, no thread | 30 packets a second does not need a thread, and there is nothing to lock |
| **Headless editor scripting** | `Scripts/*.py` run with `UnrealEditor-Cmd -run=pythonscript` | Import 190 FBX files, make the level, materials and audio without opening the editor. Repeatable by any teammate |

## 4. Game flow

```mermaid
stateDiagram-v2
    [*] --> Title
    Title --> Tutorial : finger gun seen (or mouse)
    Tutorial --> Bell : both cans shot
    Bell --> Ride : bell shot, train departs
    state Ride {
        [*] --> Riders
        Riders --> Boarders : 28 s
        Boarders --> SecondTrain : 28 s
        SecondTrain --> [*] : train drops back
    }
    Ride --> Showdown : boss lands
    Showdown --> Ride : boss killed, lap + 1, night or dawn, new gun
    Ride --> Result : third hat lost
    Showdown --> Result : third hat lost
    Result --> [*] : shoot RIDE AGAIN, level reloads
```

What each stage queues into the world (it turns up 10 to 17 s later, in order):

```mermaid
flowchart LR
    R[Riders] -->|t=0| town[Town street + station]
    R -->|t=6 s| bridge[Trestle bridge<br/>riders rein in]
    B[Boarders] -->|t=0| canyon[Deep canyon x6]
    B -->|t=6 s| tunnel[Tunnel: duck, nobody spawns]
    B -->|near the end| side[Side track, length from speed]
    S[Second train] --> crew[Crew climbs up, barrels on roofs]
    S --> boss[Boss: holster, wait, DRAW]
```

## 5. One frame

```mermaid
flowchart TD
    A[UFGTrackerInput tick<br/>TG_PrePhysics] --> A1[read all UDP packets<br/>state, fire, reload, camera]
    A1 --> A2[smooth aim by distance<br/>ride through dropouts]
    A2 --> B[AFGTrainPlayer tick]
    B --> B1[lean and duck into Lohith's SetBodyInput<br/>duck into the spring arm offset]
    B1 --> B2[pose the gun toward the crosshair]
    B2 --> C[AFGIronHorseGameMode tick]
    C --> C1[train speed toward TargetSpeed<br/>world.SetDistance]
    C1 --> C2[streamer: lay chunks ahead, drop behind,<br/>move the chain under the train]
    C2 --> C3[place both trains car by car]
    C3 --> C4[phase tick: director, showdown]
    C4 --> C5[aim magnet, enemy shots in flight,<br/>duck check, tunnel roof, sky, music]
    C5 --> D[AFGBandit ticks: state machines]
    D --> E[AFGHud: draw from game mode + input state]
```

## 6. A shot, end to end

```mermaid
sequenceDiagram
    participant H as Hand
    participant T as Tracker
    participant I as UFGTrackerInput
    participant P as AFGTrainPlayer
    participant L as Lohith's Fire()
    participant G as GameMode
    participant B as AFGBandit
    H->>T: thumb drops (2 frames to confirm)
    T->>T: rewind aim to before the gesture
    T->>I: /fg/fire x y
    I->>P: OnFire(aim)
    P->>G: PlayerMayFire? UI shot?
    P->>L: Fire(): ammo, cooldown, world hitscan
    P->>G: ResolvePlayerShot(ray, muzzle)
    G->>G: nearest target within 7 degrees, head box, weapon spread or pierce
    G->>B: ApplyPointDamage
    B->>B: TakeDamage (base) then Die (ours): animation, hat flies, falls off
    B->>G: OnBanditKilled: score, headshot wins a hat
    G-->>P: flash, tracer, sound
    T->>H: glove buzz (if fitted)
```

## 7. A bandit

```mermaid
stateDiagram-v2
    [*] --> Entering : gallop up / climb aboard / drop in (boss)
    Entering --> Idle
    Idle --> Telegraph : timer up AND on screen AND got a token
    Telegraph --> Telegraph : 0.7 s, barrel flashes red, HUD ring closes
    Telegraph --> Idle : fires, releases token, waits 1.6 to 3.4 s x difficulty
    Idle --> Leaving : stage over, gap ahead, tunnel mouth
    Entering --> Dead : shot
    Idle --> Dead : shot
    Telegraph --> Dead : shot, token released
    Dead --> [*] : left behind by the train
    Leaving --> [*]
```

An enemy bullet is aimed at where the head was when fired and takes 0.7 s (down to 0.45 s late in a run) to arrive. At arrival the game checks the head's distance to that point: inside 32 cm is a hit, otherwise a whiz and a dodge on the score sheet.

## 8. The chunk streamer

```mermaid
flowchart TD
    J[chunks.json<br/>joints, centreline every 5 m, events, rigs] --> S[AFGWorldStreamer]
    Q[Director's queue<br/>'Tunnel_Entry_A', 'Flat_A+town'] --> pick
    S --> pick{next chunk}
    pick -->|queued and joint fits| place
    pick -->|else weighted random legal follower| place[Append: start = previous start x previous end connector]
    place --> dress[rigs: water tower, bell, windmill<br/>towns: buildings and props<br/>tunnels: widened and raised]
    place --> chain[(chain of placed chunks)]
    chain --> pose[track pose at distance s:<br/>interpolate the centreline]
    pose --> inv[ChainRoot transform = inverse of pose at the player]
    pose --> cars[each train car = pose at s + its offset]
    chain --> ev[MetresTo duck / dark / trestle / side_track]
    chain --> drop[drop chunks 70 m behind, keep 450 m ahead]
```

Joint rule: chunk B may follow A when `B.start_joint == A.end_joint` (`O` open desert, `C` deep canyon, `T` tunnel, `OD` with side track). Random picks stay in open desert; canyons, tunnels and side tracks only appear when the director asks, and with nothing queued the streamer heads for the way out.

## 9. Tracker pipeline

```mermaid
flowchart TD
    F[Frame: hand + pose landmarks] --> V[Reject hands that cannot be the player's:<br/>farther than the chest, not on an arm, duplicates]
    V --> G[Pick the gun hand: sticky lock, arm identity from the body model]
    G --> A[Raw aim = fingertip travel / hand scale - chest travel / chest scale]
    A --> O[One Euro filter, 30 ms lead while moving]
    O --> M[Map to screen: learned centre, 46 cm per screen,<br/>slow pull at the edges]
    O --> Hst[(aim history 1 s)]
    G --> Th[Thumb feature / palm size:<br/>relative drop, hysteresis, only when steady]
    Th -->|onset time| Rw[Rewind: median or line fit before the onset]
    Hst --> Rw --> Fire[/fg/fire/]
    G --> Sl[Hands' relation, approach speed, kicks, vanish] --> Rl[/fg/reload/]
    F --> Bd[Shoulders + head assist: lean, duck, holster, speed] --> St[/fg/state/]
    M --> St
```

Tuning loop: play, the session is recorded as landmarks, replay it offline through this exact pipeline, change a threshold, compare numbers. 55 automated tests guard the behaviour.

## 10. Co-op

Two riders on one roof, each with their own webcam on their own computer. One computer hosts (an Unreal listen server on UDP 7777), the other joins it by address. The host runs the game; the other machine shows it.

```mermaid
flowchart LR
    subgraph hostpc [Host computer]
        t1[Tracker] -->|UDP 7000| p1[Host's player]
        gm[AFGIronHorseGameMode<br/>runs everything] --> gs[AFGGameState]
        gm --> bandits[AFGBandit / AFGTarget]
        pres1[AFGPresentation<br/>line, trains, sky]
    end
    subgraph guestpc [Guest computer]
        t2[Tracker] -->|UDP 7000| p2[Guest's player]
        pres2[AFGPresentation<br/>copies the host's line]
    end
    p2 -->|ServerInput 30 Hz<br/>ServerFire| gm
    gs -->|phase, prompts, score,<br/>train clock, chunk log| pres2
    bandits -->|spec, motion, anim,<br/>warning, target| guestpc
    gs -->|shots, sounds, effects<br/>multicast| guestpc
```

- **The host decides, everyone draws.** `AFGIronHorseGameMode` exists only on the host. At the end of every frame it copies what the screens need into `AFGGameState` (`PublishState`). Timers go over as server times, never as countdowns.
- **The world is not replicated.** Each machine has its own `AFGPresentation`, which owns the chunk streamer, both trains, sky, fog and sound. The host writes every chunk it lays into a 32-slot ring on the game state (`FFGChunkRec`: which chunk, where it starts, the town's random seed). A client lays the same chunks from that ring, and carries the train forward between updates from `{distance, speed, server time}`. About 30 bytes per 50 m of track, and a late joiner gets the line from where it is.
- **Bandits ride on anchors, not attachments.** A bandit on a boxcar is stored in that car's frame (`FFGAnchor`), and each machine works out where the car is itself. Their AI runs on the host. Clients get what each bandit is, where it is in its frame and how fast that is changing, its animation, when its barrel flashes and who it is aiming at.
- **Your hands stay yours.** Each player's tracker drives only their own player (`UFGTrackerInput` opens its ports only for the local player, or a host would take its partner's packets). Aim, lean, duck and holster go to the host about 30 times a second; the partner sees a cowboy on the roof that ducks and aims with them.
- **Shots.** The shooter sees and hears their shot at once, aimed where their own screen thinks it lands. `ServerFire` sends the ray; the host decides what it hit with the same aim assist as solo (`FGCombat::FindHit`) and shows the shot to everyone else.
- **Dodging across a network.** A bullet aimed at a player over the network is judged a round trip later than one aimed at the host, so a dodge made in time on that player's own screen counts. The duel's draw time is measured on the shooter's own screen from when DRAW! reached it.

The co-op rules, on top of solo:

| | |
| --- | --- |
| Hats | Each rider has three. Out of hats, you're down: no shooting, and bandits ignore you. |
| Revive | A headshot gives a hat to whoever needs it most. A partner who is down comes back with it. Beating the boss brings everyone back. |
| Bandits | Each shot picks a rider who can see it coming and has the fewest guns on them. The warning ring is red when it's you, amber when it's your partner. Two riders get a few more bandits. |
| Hazards | Duck bars, signal arms and tunnels are judged on each rider where they stand. |
| Duel | Both holster. Either one firing early resets the wait. The boss draws on both. |
| End | When both are down. The poster has a column each. Both shoot to ride again. |

**1v1 duel** (`?versus` on the host's URL): the same machinery with the two players seated at either end of the passenger car roof facing each other (`SeatYaw`), the line kept straight and 450 m long behind the train as well as ahead, and `AFGIronHorseGameMode::TickVersus` running rounds of holster, wait, DRAW!. A shot is judged on the host against the other player's cowboy (its chest and head bones, which the host keeps posed even when nobody is looking at them). The first hit to arrive waits a round trip for a quicker one from the other side: the faster draw wins, timed on each shooter's own screen.

The main menu (`AFGPlayerController::IsMenuOpen`) is solo only, drawn by the HUD and picked by shooting it like anything else. Hosting reopens the level with `?listen` (or `?listen?versus`); leaving reopens it plain.

Testing on one Mac: `unreal/FingerGunGame/Scripts/coop_test.sh` starts a host and a guest side by side, both playing themselves, and the `IronHorse: chunk N` lines in `Saved/host.log` and `Saved/guest.log` must match.
