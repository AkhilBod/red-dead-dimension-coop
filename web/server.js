// Red Dead Dimension on the web: the game's files, and one WebSocket per player for the duel.
//   npm start            (PORT from the environment, 8080 locally)
import express from 'express';
import http from 'node:http';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';
import { Rooms } from './game/rooms.js';
import { RULES } from './public/js/shared.js';

const here = path.dirname(fileURLToPath(import.meta.url));
// Tests only: the same duel with short pauses, so a whole match takes seconds.
if (process.env.RDD_FAST) { Object.assign(RULES, { introMs: 300, holsterMaxMs: 300, waitMinMs: 300, waitMaxMs: 400, pointMs: 300, pauseMs: 3000, drawMaxMs: 3000, graceMs: 1500 }); }
const PORT = Number(process.env.PORT) || 8080;
const rooms = new Rooms();

const app = express();
app.disable('x-powered-by');
app.get('/healthz', (req, res) => res.json({ ok: true, rooms: rooms.rooms.size }));
app.use('/assets', express.static(path.join(here, 'public/assets'), { maxAge: '7d', immutable: true }));
app.use(express.static(path.join(here, 'public'), { maxAge: 0 }));

const server = http.createServer(app);
const wss = new WebSocketServer({ server, path: '/ws', maxPayload: 4096 });

wss.on('connection', (ws) => {
  let seat = null;                      // { room, player } once in a room
  let budget = 60;                      // messages per second: a broken or hostile page cannot flood the room
  const refill = setInterval(() => { budget = 60; }, 1000);
  const send = (msg) => { if (ws.readyState === ws.OPEN) { ws.send(JSON.stringify(msg)); } };
  const fail = (message, error = 'error') => send({ t: 'error', error, message });

  ws.on('message', (data) => {
    if (--budget < 0) { return; }
    let msg;
    try { msg = JSON.parse(data); } catch { return; }
    if (!msg || typeof msg.t !== 'string') { return; }
    if (msg.t === 'ping') { send({ t: 'pong', c: msg.c, s: Date.now() }); if (seat && Number.isFinite(msg.rtt)) { seat.room.ping(seat.player, msg.rtt); } return; }

    if (msg.t === 'create' || msg.t === 'join') {
      if (seat) { seat.room.disconnect(seat.player); }
      const res = msg.t === 'create' ? rooms.create(msg.name, send) : rooms.join(msg.code, msg.name, msg.token, send);
      if (res.error) { seat = null; fail(res.message, res.error); return; }
      seat = res;
      send({ t: 'joined', code: res.room.code, id: res.player.id, token: res.player.token });
      res.room.dirty = true;
      res.room.flush();
      return;
    }
    if (!seat) { return; }
    const { room, player } = seat;
    switch (msg.t) {
      case 'input': room.input(player, msg); break;
      case 'shot': { const out = room.shot(player, msg); if (out) { room.broadcast(out); } break; }
      case 'start': { const why = room.start(player); if (why) { fail(why); } break; }
      case 'rematch': room.rematch(player); break;
      case 'leave': room.leave(player); room.flush(); seat = null; break;
      default: break;
    }
  });

  ws.on('close', () => {
    clearInterval(refill);
    if (seat && seat.player.send === send) { seat.room.disconnect(seat.player); }
  });
});

// The game runs here: every room's clock, 20 times a second, and its news to its players.
setInterval(() => {
  rooms.tick();
  for (const room of rooms.rooms.values()) { room.flush(); }
}, 50);

server.listen(PORT, () => console.log(`Red Dead Dimension on http://localhost:${PORT}`));
