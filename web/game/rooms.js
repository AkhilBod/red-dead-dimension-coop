// Rooms and the 1v1 duel. The server owns the game: the clock, every round, every shot. Browsers only send what their
// player does (body, aim, shots) and draw what they are told.
import crypto from 'node:crypto';
import { RULES, ROOM_CODE, ROOM_LETTERS, hitTest, normalize, pose } from '../public/js/shared.js';

const MAX_PLAYERS = 2;
const EMPTY_ROOM_MS = 120000;       // nobody connected for this long: the room is gone

const num = (v, lo, hi, dflt = 0) => (Number.isFinite(v) ? Math.min(hi, Math.max(lo, v)) : dflt);
const vec = (v) => (Array.isArray(v) && v.length === 3 && v.every(Number.isFinite) ? v.map(Number) : null);

export class Rooms {
  constructor(now = () => Date.now()) {
    this.now = now;
    this.rooms = new Map();
  }

  newCode() {
    for (let tries = 0; tries < 1000; ++tries) {
      let code = '';
      for (let i = 0; i < 4; ++i) { code += ROOM_LETTERS[crypto.randomInt(ROOM_LETTERS.length)]; }
      if (!this.rooms.has(code)) { return code; }
    }
    throw new Error('no free room codes');
  }

  create(name, send) {
    const room = new Room(this.newCode(), this.now);
    this.rooms.set(room.code, room);
    return room.add(name, send);
  }

  /** Join by code, or come back to your own seat with your token. */
  join(code, name, token, send) {
    code = String(code || '').trim().toUpperCase();
    if (!ROOM_CODE.test(code)) { return { error: 'bad_code', message: 'Room codes are 4 letters, like KQTX.' }; }
    const room = this.rooms.get(code);
    if (!room) { return { error: 'no_room', message: `There is no room ${code}. Check the code, or create a new room.` }; }
    const back = token && room.players.find((p) => p.token === token);
    if (back) { return room.rejoin(back, send); }
    if (room.players.length >= MAX_PLAYERS) {
      const dropped = room.players.some((p) => !p.connected);
      return { error: 'full', message: dropped ? `Room ${code} is full. One player dropped out: their place frees up within ${Math.round(RULES.graceMs / 1000)} seconds.` : `Room ${code} already has two players.` };
    }
    return room.add(name, send);
  }

  tick() {
    const now = this.now();
    for (const [code, room] of this.rooms) {
      room.tick();
      if (room.players.every((p) => !p.connected) && now - room.lastActive > EMPTY_ROOM_MS) { this.rooms.delete(code); }
    }
  }
}

export class Room {
  constructor(code, now) {
    this.code = code;
    this.now = now;
    this.seed = crypto.randomInt(1 << 30);
    this.players = [];
    this.hostId = null;
    this.phase = 'lobby';            // lobby, match, results
    this.step = null;                // intro, holster, wait, draw, point, paused
    this.round = 0;
    this.stepAt = 0;
    this.stepEnds = 0;
    this.drawAt = 0;
    this.holsteredSince = 0;
    this.pending = null;             // the first hit of a round, waiting a round trip for a faster one
    this.banner = null;
    this.result = null;              // { winnerId, reason }
    this.pausedStep = null;
    this.lastActive = now();
    this.rideStart = now();          // the train's distance comes from this, for scenery both screens agree on
    this.dirty = true;
  }

  // ---------------------------------------------------------------- players

  add(name, send) {
    const seat = [0, 1].find((s) => !this.players.some((p) => p.seat === s));
    const player = {
      id: crypto.randomUUID().slice(0, 8),
      token: crypto.randomUUID(),
      name: String(name || '').replace(/[^\p{L}\p{N} ._'-]/gu, '').trim().slice(0, 14) || `Player ${seat + 1}`,
      seat,
      send,
      connected: true,
      lastSeen: this.now(),
      input: { lean: 0, duck: 0, holster: false, aiming: false, webcam: false },
      wins: 0, hats: RULES.hats, best: -1, fouls: 0, shots: 0, hits: 0,
      ammo: RULES.ammo, lastShotAt: 0, rtt: 100, rematch: false,
    };
    this.players.push(player);
    if (!this.hostId) { this.hostId = player.id; }
    this.lastActive = this.now();
    this.dirty = true;
    return { room: this, player };
  }

  rejoin(player, send) {
    const old = player.send;
    player.send = send;
    player.connected = true;
    player.lastSeen = this.now();
    if (old && old !== send) { old({ t: 'replaced' }); }           // the same player opened a second tab
    if (this.step === 'paused' && this.players.length === MAX_PLAYERS && this.players.every((p) => p.connected)) { this.resume(); }
    this.dirty = true;
    return { room: this, player };
  }

  disconnect(player) {
    if (!player.connected) { return; }
    player.connected = false;
    player.lastSeen = this.now();
    this.lastActive = this.now();
    if (this.phase === 'match' && this.step !== 'paused') { this.pause(player); }
    this.dirty = true;
  }

  /** Left on purpose (the Leave button): gone at once, and a duel in progress goes to the other player. */
  leave(player) {
    this.players = this.players.filter((p) => p !== player);
    if (this.hostId === player.id) { this.hostId = this.players[0]?.id ?? null; }
    if (this.phase === 'match' && this.players.length) { this.finish(this.players[0], 'left'); }
    else if (this.phase === 'results') { this.phase = 'lobby'; this.result = null; }   // the one left waits for a new opponent
    this.lastActive = this.now();
    this.dirty = true;
  }

  opponent(player) {
    return this.players.find((p) => p !== player);
  }

  // ---------------------------------------------------------------- messages

  start(player) {
    if (player.id !== this.hostId || this.phase !== 'lobby') { return 'Only the host can start, from the lobby.'; }
    if (this.players.length < MAX_PLAYERS || !this.players.every((p) => p.connected)) { return 'Waiting for a second player.'; }
    this.beginMatch();
    return null;
  }

  rematch(player) {
    if (this.phase !== 'results') { return; }
    player.rematch = true;
    if (this.players.length === MAX_PLAYERS && this.players.every((p) => p.rematch && p.connected)) { this.beginMatch(); }
    this.dirty = true;
  }

  input(player, msg) {
    const i = player.input;
    i.lean = num(msg.lean, -1, 1);
    i.duck = num(msg.duck, 0, 1);
    i.holster = Boolean(msg.holster);
    i.aiming = Boolean(msg.aiming);
    i.webcam = Boolean(msg.webcam);
    this.dirty = true;
  }

  ping(player, rtt) {
    player.rtt = num(rtt, 0, 2000, player.rtt);
  }

  /** A shot. Returns what to tell everyone (or null for a shot that does not count). */
  shot(player, msg) {
    const now = this.now();
    if (this.phase !== 'match' || (this.step !== 'wait' && this.step !== 'draw')) { return null; }
    if (now - player.lastShotAt < RULES.shotGapMs || player.ammo <= 0) { return null; }
    let origin = vec(msg.o);
    let dir = vec(msg.d);
    if (!dir) { return null; }
    dir = normalize(dir);
    // The shot starts at the shooter's own eye, near enough. (The browser knows its camera better than the server
    // knows their lean, but not by more than this.)
    const me = pose(player.seat, player.input.lean, player.input.duck);
    if (!origin || Math.hypot(origin[0] - me.eye[0], origin[1] - me.eye[1], origin[2] - me.eye[2]) > 0.7) { origin = me.eye; }
    player.lastShotAt = now;
    player.ammo -= 1;
    player.shots += 1;
    const them = this.opponent(player);

    if (this.step === 'wait') {
      // Before DRAW!: the round goes to the other one.
      player.fouls += 1;
      const end = [origin[0] + dir[0] * 40, origin[1] + dir[1] * 40, origin[2] + dir[2] * 40];
      if (them) { this.winRound(them, -1, true); }
      return { t: 'shot', by: player.id, o: origin, e: end, hit: false };
    }

    const target = them ? pose(them.seat, them.input.lean, them.input.duck) : null;
    const res = target ? hitTest(origin, dir, target) : { hit: false, point: origin };
    if (res.hit && them) {
      player.hits += 1;
      // Fastest draw wins, timed on the shooter's own screen from when DRAW! appeared there, so a slower connection
      // does not decide it. It cannot be claimed faster than the shot could have been fired, though.
      const elapsed = now - this.drawAt;
      const claimed = num(msg.react, 0, 30000, elapsed - player.rtt / 2);
      const react = Math.round(Math.min(elapsed + 30, Math.max(elapsed - player.rtt - 150, claimed)));
      if (!this.pending) {
        const window = Math.min(300, Math.max(60, ...this.players.map((p) => p.rtt)));
        this.pending = { id: player.id, react, until: now + window };
      } else if (this.pending.id !== player.id && react < this.pending.react) {
        this.pending = { ...this.pending, id: player.id, react };
      }
    }
    return { t: 'shot', by: player.id, o: origin, e: res.point, hit: res.hit, part: res.part };
  }

  // ---------------------------------------------------------------- the duel

  beginMatch() {
    this.phase = 'match';
    this.round = 0;
    this.result = null;
    for (const p of this.players) {
      Object.assign(p, { wins: 0, hats: RULES.hats, best: -1, fouls: 0, shots: 0, hits: 0, rematch: false });
    }
    this.nextRound(RULES.introMs);
  }

  nextRound(introMs = 0) {
    this.round += 1;
    this.pending = null;
    for (const p of this.players) { p.ammo = RULES.ammo; }
    if (introMs) { this.setStep('intro', introMs); } else { this.setStep('holster', RULES.holsterMaxMs); }
  }

  setStep(step, ms) {
    this.step = step;
    this.stepAt = this.now();
    this.stepEnds = this.stepAt + ms;
    this.holsteredSince = 0;
    this.dirty = true;
  }

  pause(player) {
    this.pausedStep = this.step;
    this.pending = null;
    this.setStep('paused', RULES.pauseMs);
    this.banner = { text: `${player.name} LOST CONNECTION`, until: this.stepEnds };
  }

  resume() {
    // The round they dropped in is played again from the holster: nobody wins a round off a dropped line.
    this.banner = { text: 'BACK ON.  SAME ROUND AGAIN', until: this.now() + 2000 };
    this.pending = null;
    for (const p of this.players) { p.ammo = RULES.ammo; }
    this.setStep('holster', RULES.holsterMaxMs);
  }

  winRound(winner, react, foul) {
    const loser = this.opponent(winner);
    this.pending = null;
    winner.wins += 1;
    if (!foul && react >= 0) { winner.best = winner.best < 0 ? react : Math.min(winner.best, react); }
    if (loser) { loser.hats = Math.max(0, loser.hats - 1); }
    this.banner = foul
      ? { text: `${loser?.name ?? 'THEY'} DREW EARLY`, sub: `${winner.name} takes the round`, winnerId: winner.id, loserId: loser?.id, foul: true, until: this.now() + RULES.pointMs }
      : { text: `${winner.name} WINS THE ROUND`, sub: `${react} ms`, winnerId: winner.id, loserId: loser?.id, ms: react, until: this.now() + RULES.pointMs };
    this.setStep('point', RULES.pointMs);
  }

  finish(winner, reason) {
    this.phase = 'results';
    this.step = null;
    this.pending = null;
    this.result = { winnerId: winner?.id ?? null, reason };
    for (const p of this.players) { p.rematch = false; }
    this.dirty = true;
  }

  tick() {
    const now = this.now();
    // Places kept for a refresh are given up after a while (except in a duel, where a drop pauses it instead).
    if (this.phase !== 'match') {
      const gone = this.players.filter((p) => !p.connected && now - p.lastSeen > RULES.graceMs);
      for (const p of gone) { this.leave(p); }
    }
    if (this.phase !== 'match') { return; }
    switch (this.step) {
      case 'intro':
        if (now >= this.stepEnds) { this.setStep('holster', RULES.holsterMaxMs); }
        break;
      case 'holster': {
        // Webcam players drop their gun hand to the hip; with a mouse or a screen it is down until DRAW! anyway.
        const all = this.players.every((p) => p.input.holster || !p.input.webcam);
        if (all && !this.holsteredSince) { this.holsteredSince = now; }
        if (!all) { this.holsteredSince = 0; }
        if ((this.holsteredSince && now - this.holsteredSince >= RULES.holsterMs) || now >= this.stepEnds) {
          this.setStep('wait', RULES.waitMinMs + Math.random() * (RULES.waitMaxMs - RULES.waitMinMs));
        }
        break;
      }
      case 'wait':
        if (now >= this.stepEnds) {
          this.drawAt = now;
          this.setStep('draw', RULES.drawMaxMs);
        }
        break;
      case 'draw':
        if (this.pending && now >= this.pending.until) {
          const winner = this.players.find((p) => p.id === this.pending.id);
          if (winner) { this.winRound(winner, this.pending.react, false); }
        } else if (!this.pending && now >= this.stepEnds) {
          this.banner = { text: 'NOBODY HIT', sub: 'same round again', until: now + 2000 };
          this.round -= 1;
          this.setStep('point', 2000);
        }
        break;
      case 'point':
        if (now >= this.stepEnds) {
          const out = this.players.find((p) => p.hats <= 0);
          if (out) { this.finish(this.opponent(out), 'hats'); }
          else { this.nextRound(); }
        }
        break;
      case 'paused':
        if (now >= this.stepEnds) {
          // They did not come back: the one still here wins.
          const stayed = this.players.find((p) => p.connected);
          this.finish(stayed, 'disconnect');
        }
        break;
      default:
        break;
    }
  }

  /** What one player is told. Nobody is ever sent anyone else's token. */
  view(forPlayer) {
    return {
      t: 'state',
      now: this.now(),
      code: this.code,
      seed: this.seed,
      rideStart: this.rideStart,
      you: forPlayer.id,
      hostId: this.hostId,
      phase: this.phase,
      step: this.step,
      round: this.round,
      stepAt: this.stepAt,
      stepEnds: this.stepEnds,
      drawAt: this.step === 'draw' ? this.drawAt : 0,
      banner: this.banner && this.banner.until > this.now() ? this.banner : null,
      result: this.result,
      players: this.players.map((p) => ({
        id: p.id, name: p.name, seat: p.seat, connected: p.connected,
        wins: p.wins, hats: p.hats, best: p.best, fouls: p.fouls, shots: p.shots, hits: p.hits, ammo: p.ammo,
        rematch: p.rematch, input: p.input,
      })),
    };
  }

  broadcast(msg) {
    for (const p of this.players) { if (p.connected) { p.send(msg); } }
  }

  flush() {
    if (!this.dirty) { return; }
    this.dirty = false;
    for (const p of this.players) { if (p.connected) { p.send(this.view(p)); } }
  }
}
