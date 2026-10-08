# Red Dead Dimension: the 1v1 duel in a browser

https://red-dead-dimension.azurewebsites.net

A quick-draw duel on the roof of a moving train, for two players on any devices. Same models and sounds as the Unreal game (`art/export`, `unreal/FingerGunGame/Scripts/audio_src`).

- **Rooms:** create one, send the 4-letter code or the invite link (`/?room=KQTX`). Two players a room; a third is told it is full.
- **Rounds:** holster, WAIT FOR IT..., DRAW!. First hit wins the round, firing before DRAW! loses it. Three hats each. Duel again in the same room.
- **Controls:** mouse (click to shoot, A/D lean, S duck), touch (tap to shoot, hold the buttons to lean and duck), or a webcam finger gun (MediaPipe hand tracking in the browser: drop your thumb to fire, lower your hand to holster).

## How it works

- `server.js` serves the page and runs one WebSocket per player. `game/rooms.js` is the game: room codes, the lobby, every round on the server clock, every shot judged on the server, drops paused and resumed, seats kept through a refresh.
- `public/js/shared.js` is the geometry both sides use: where each cowboy is for a given lean and duck, and whether a shot hits. The browser draws exactly what the server judges.
- Draw times are measured on each player's own screen from when DRAW! appeared there, and checked against when the shot reached the server, so a slower connection does not decide who was faster.
- `public/js/world.js` is the three.js scene: the train stands still and the desert moves under it, from the room's seed so both players see the same scenery.

## Run it

```bash
cd web
npm install
npm start          # http://localhost:8080
npm test           # plays full duels against a local server: codes, capacity, fouls, hits, drops, rematch
```

`npm run assets` rebuilds `public/assets` from `art/export` (FBX files go through Blender first: `tools/convert_fbx.py`).

## Deploy

```bash
az login
web/infra/deploy.sh                # infra/main.bicep (previewed with what-if), then the code
RDD_URL=https://red-dead-dimension.azurewebsites.net npm test   # the same duel checks against the live game
```

One Linux App Service (B1), Node 22, WebSockets on, HTTPS only. Rooms live in memory, so it runs as one instance.
