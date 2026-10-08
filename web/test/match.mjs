// Plays real duels against a real server over WebSockets and checks the rules, the room system and the edge cases.
//   npm test                                                       a local server with short pauses (seconds)
//   RDD_URL=https://red-dead-dimension.azurewebsites.net npm test  the deployed game, real timings (a few minutes)
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import WebSocket from 'ws';
import { pose } from '../public/js/shared.js';

const REMOTE = process.env.RDD_URL;
const SLOW = REMOTE ? 12 : 1;                    // real pauses on the deployed server: 30 s grace instead of 1.5 s
const PORT = 18000 + Math.floor(Math.random() * 1000);
const WS_URL = REMOTE ? `${REMOTE.replace(/^http/, 'ws').replace(/\/$/, '')}/ws` : `ws://localhost:${PORT}/ws`;
const server = REMOTE ? null : spawn(process.execPath, ['server.js'], { env: { ...process.env, PORT: String(PORT), RDD_FAST: '1' }, stdio: ['ignore', 'pipe', 'inherit'] });
process.on('exit', () => server?.kill());
process.on('uncaughtException', (e) => { console.error(e); server?.kill(); process.exit(1); });
process.on('unhandledRejection', (e) => { console.error(e); server?.kill(); process.exit(1); });
if (server) { await new Promise((resolve) => server.stdout.on('data', (d) => { if (String(d).includes('localhost')) { resolve(); } })); }
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

class Client {
  constructor(name) { this.name = name; this.msgs = []; this.waiters = []; this.state = null; }
  open() {
    return new Promise((resolve) => {
      this.ws = new WebSocket(WS_URL);
      this.ws.on('open', resolve);
      this.ws.on('message', (d) => {
        const m = JSON.parse(d);
        this.msgs.push(m);
        if (m.t === 'state') { this.state = m; }
        if (m.t === 'joined') { this.id = m.id; this.token = m.token; this.code = m.code; }
        this.waiters = this.waiters.filter((w) => !w(m));
      });
    });
  }
  send(m) { this.ws.send(JSON.stringify(m)); }
  next(pred, ms = 8000 * SLOW) {
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error(`${this.name}: timed out`)), ms);
      const w = (m) => { if (!pred(m)) { return false; } clearTimeout(t); resolve(m); return true; };
      this.waiters.push(w);
    });
  }
  until(pred, ms) { return this.state && pred(this.state) ? Promise.resolve(this.state) : this.next((m) => m.t === 'state' && pred(m), ms); }
  me(s = this.state) { return s.players.find((p) => p.id === this.id); }
  aimAt(other, part = 'head', react) {
    const s = this.state;
    const mine = this.me(s), theirs = s.players.find((p) => p.id !== this.id);
    const from = pose(mine.seat, mine.input.lean, mine.input.duck).eye;
    const tp = pose(theirs.seat, theirs.input.lean, theirs.input.duck);
    const to = part === 'head' ? tp.head : part === 'miss' ? [tp.head[0], tp.head[1] + 2, tp.head[2] + 3] : tp.shoulder;
    this.send({ t: 'shot', o: from, d: [to[0] - from[0], to[1] - from[1], to[2] - from[2]], react });
  }
}

const ok = (label) => console.log(`ok  ${label}`);
const a = new Client('A'), b = new Client('B'), c = new Client('C');
await Promise.all([a.open(), b.open(), c.open()]);

// codes
c.send({ t: 'join', code: '12', name: 'C' });
assert.equal((await c.next((m) => m.t === 'error')).error, 'bad_code');
c.send({ t: 'join', code: 'QQQQ', name: 'C' });
assert.equal((await c.next((m) => m.t === 'error')).error, 'no_room');
ok('a malformed code and an unknown code each get a clear error');

// lobby
a.send({ t: 'create', name: 'Ana' });
await a.next((m) => m.t === 'joined');
assert.match(a.code, /^[A-HJ-NP-Z]{4}$/);
b.send({ t: 'join', code: a.code.toLowerCase(), name: 'Ben' });
await b.next((m) => m.t === 'joined');
await a.until((s) => s.players.length === 2);
c.send({ t: 'join', code: a.code, name: 'Cal' });
assert.equal((await c.next((m) => m.t === 'error')).error, 'full');
ok('room code created, joined (any case), third player turned away');
assert.equal(a.state.hostId, a.id);
assert.ok(!JSON.stringify(a.state).includes(b.token), 'never sent the other player\'s token');
b.send({ t: 'start' });
assert.match((await b.next((m) => m.t === 'error')).message, /host/i);
ok('only the host can start; tokens stay private');

// a refresh in the lobby keeps the seat
b.ws.close();
await a.until((s) => !s.players.find((p) => p.id === b.id).connected);
const b2 = new Client('B2');
await b2.open();
b2.send({ t: 'join', code: a.code, token: b.token });
await b2.next((m) => m.t === 'joined');
assert.equal(b2.id, b.id);
await a.until((s) => s.players.length === 2 && s.players.every((p) => p.connected));
ok('reconnecting with the token takes back the same seat: no duplicate player');

// the duel
a.send({ t: 'start' });
await a.until((s) => s.phase === 'match');
await a.until((s) => s.step === 'wait');
b2.aimAt(a, 'head');                                    // before DRAW!
const foul = await a.until((s) => s.step === 'point');
assert.equal(foul.players.find((p) => p.id === a.id).wins, 1);
assert.equal(foul.players.find((p) => p.id === b2.id).fouls, 1);
assert.equal(foul.players.find((p) => p.id === b2.id).hats, 2);
ok('firing before DRAW! loses the round');

await a.until((s) => s.step === 'draw');
await sleep(250);
b2.aimAt(a, 'miss', 250);
await sleep(50);
a.aimAt(b2, 'head', 300);
const r2 = await a.until((s) => s.step === 'point' && s.round === 2);
assert.equal(r2.players.find((p) => p.id === a.id).wins, 2);
ok('a miss does not count; a head shot after DRAW! wins the round');

await a.until((s) => s.step === 'draw');
await sleep(400);
a.aimAt(b2, 'body', 400);                               // arrives first...
await sleep(20);
b2.aimAt(a, 'body', 210);                               // ...but this one was drawn faster on its own screen
const r3 = await a.until((s) => s.step === 'point' && s.round === 3);
assert.equal(r3.players.find((p) => p.id === b2.id).wins, 1, 'the faster draw wins, not the first packet');
ok('near-simultaneous hits go to the faster draw, timed on each screen');

// a drop mid-duel pauses it; coming back replays the round
await a.until((s) => s.step === 'holster' || s.step === 'wait' || s.step === 'draw');
b2.ws.close();
const paused = await a.until((s) => s.step === 'paused');
assert.match(paused.banner.text, /LOST CONNECTION/);
const b3 = new Client('B3');
await b3.open();
b3.send({ t: 'join', code: a.code, token: b.token });
await b3.next((m) => m.t === 'joined');
await a.until((s) => s.step === 'holster' && s.players.every((p) => p.connected));
assert.equal(a.state.players.length, 2);
ok('a dropped player pauses the duel and comes back to the same round');

// play it out: A wins
while (a.state.phase === 'match') {
  await a.until((s) => s.step === 'draw' || s.phase !== 'match', 10000 * SLOW);
  if (a.state.phase !== 'match') { break; }
  await sleep(300);
  a.aimAt(b3, 'head', 300);
  await a.until((s) => s.step !== 'draw', 10000 * SLOW);
}
const done = await a.until((s) => s.phase === 'results');
assert.equal(done.result.winnerId, a.id);
assert.equal(done.players.find((p) => p.id === b3.id).hats, 0);
assert.ok(done.players.find((p) => p.id === a.id).best > 0);
ok('three hats down: the right winner, with best draw times');

// rematch in the same room
a.send({ t: 'rematch' });
await a.until((s) => s.players.find((p) => p.id === a.id).rematch);
assert.equal(a.state.phase, 'results', 'one vote is not enough');
b3.send({ t: 'rematch' });
const again = await a.until((s) => s.phase === 'match' && s.round === 1);
assert.ok(again.players.every((p) => p.hats === 3 && p.wins === 0));
ok('both ask for a rematch: a fresh duel in the same room');

// a drop that never comes back
b3.ws.close();
const gone = await a.until((s) => s.phase === 'results', 8000 * SLOW);
assert.equal(gone.result.reason, 'disconnect');
assert.equal(gone.result.winnerId, a.id);
ok('a player who never comes back loses the duel to the one who stayed');

// leaving
assert.equal(gone.players.length, 2, 'their place is kept for a while in case they come back');
await a.until((s) => s.players.length === 1, 8000 * SLOW);
const d = new Client('D');
await d.open();
d.send({ t: 'join', code: a.code, name: 'Dee' });
const dj = await d.next((m) => m.t === 'joined' || m.t === 'error');
assert.equal(dj.t, 'joined', 'the gone player\'s seat frees up after the grace period');
await a.until((s) => s.phase === 'lobby' && s.players.length === 2, 40000 * SLOW);
a.send({ t: 'start' });
await d.until((s) => s.phase === 'match');
a.send({ t: 'leave' });
const left = await d.until((s) => s.phase === 'results');
assert.equal(left.result.reason, 'left');
assert.equal(left.hostId, d.id, 'the host role passes on');
ok('a new opponent can take the free seat; leaving mid-duel hands it over; the host role passes on');

for (const cl of [a, c, d]) { cl.ws.close(); }
server?.kill();
console.log('\nAll duel checks passed.');
process.exit(0);
