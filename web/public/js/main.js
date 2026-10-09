import { Controls } from './controls.js';
import { Net } from './net.js';
import { ROOM_CODE, RULES, hitTest, pose } from './shared.js';
import { Sound } from './sound.js';
import { FingerGun } from './webcam.js';
import { World } from './world.js';

const $ = (id) => document.getElementById(id);
const screens = ['home', 'lobby', 'results'];
const show = (id) => { for (const s of screens) { $(s).classList.toggle('hidden', s !== id); } $('hud').classList.toggle('hidden', id !== 'hud'); };
const toast = (text) => { const t = $('toast'); t.textContent = text; t.classList.add('show'); clearTimeout(toast.timer); toast.timer = setTimeout(() => t.classList.remove('show'), 2600); };
const store = {
  get(k) { try { return localStorage.getItem(k) || ''; } catch { return ''; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } },
};
const HAT = '<svg viewBox="0 0 24 14" width="30" height="18" aria-hidden="true"><path d="M1 11h22v2H1zM6 3c0-2 12-2 12 0v8H6z" fill="currentColor"/></svg>';

let state = null;            // the room, as the server last told it
let you = null;
let net = null;
let drawShownAt = 0;         // when DRAW! appeared on this screen: the draw time is measured from here
let lastStep = null;
let lastRound = 0;
let lastBanner = null;
let crosshairHitUntil = 0;
let tapShownUntil = 0;

const canvas = $('view');
const world = new World(canvas);
const sound = new Sound();
const gun = new FingerGun();
const controls = new Controls(canvas, $('touchpad'), shoot);
controls.onRecenter = () => gun.recenter();
gun.onFire = (aim, at) => shoot(aim, 'webcam', at);

const me = () => state?.players.find((p) => p.id === you);
const them = () => state?.players.find((p) => p.id !== you);

// ---------------------------------------------------------------- home

$('name').value = store.get('rdd:name');
const params = new URLSearchParams(location.search);
const invited = (params.get('room') || '').toUpperCase();
if (ROOM_CODE.test(invited)) { $('code').value = invited; }
$('code').addEventListener('input', (e) => { e.target.value = e.target.value.toUpperCase().replace(/[^A-Z]/g, '').slice(0, 4); });
const name = () => { const n = $('name').value.trim().slice(0, 14); store.set('rdd:name', n); return n; };

$('create').addEventListener('click', () => { sound.unlock(); $('home-error').textContent = ''; net.create(name()); });
const join = () => {
  sound.unlock();
  const code = $('code').value.trim().toUpperCase();
  if (!ROOM_CODE.test(code)) { $('home-error').textContent = 'Room codes are 4 letters, like KQTX.'; $('code').focus(); return; }
  $('home-error').textContent = '';
  net.join(code, name());
};
$('join').addEventListener('click', join);
$('code').addEventListener('keydown', (e) => { if (e.key === 'Enter') { join(); } });

for (const b of document.querySelectorAll('[data-open="rules"]')) { b.addEventListener('click', () => $('rules').classList.remove('hidden')); }
$('rules').addEventListener('click', (e) => { if (e.target === $('rules') || e.target.closest('[data-close]')) { $('rules').classList.add('hidden'); } });

// ---------------------------------------------------------------- lobby

$('copy').addEventListener('click', async () => {
  try { await navigator.clipboard.writeText($('invite-link').value); toast('Invite link copied'); }
  catch { $('invite-link').select(); toast('Copy the link above'); }
});
if (navigator.share) {
  $('share').classList.remove('hidden');
  $('share').addEventListener('click', () => navigator.share({ title: 'Red Dead Dimension duel', text: `Duel me: room ${state?.code}`, url: $('invite-link').value }).catch(() => {}));
}
$('start').addEventListener('click', () => { sound.unlock(); net.send({ t: 'start' }); });
const leave = () => {
  net.leave();
  state = null;
  you = null;
  history.replaceState(null, '', '/');
  show('home');
};
$('leave').addEventListener('click', leave);
$('leave2').addEventListener('click', leave);
$('rematch').addEventListener('click', () => { sound.unlock(); net.send({ t: 'rematch' }); });

const setGunChoice = (webcam) => {
  $('use-pointer').classList.toggle('on', !webcam);
  $('use-webcam').classList.toggle('on', webcam);
  $('webcam').classList.toggle('hidden', !webcam);
  $('webcam-dots').classList.toggle('hidden', !webcam);
};
$('use-pointer').addEventListener('click', () => { gun.stop(); setGunChoice(false); $('webcam-note').textContent = ''; });
$('use-webcam').addEventListener('click', async () => {
  sound.unlock();
  if (gun.on) { return; }
  $('webcam-note').textContent = 'Starting the camera and the hand tracker…';
  try {
    await gun.start($('webcam'), $('webcam-dots'));
    setGunChoice(true);
    $('webcam-note').textContent = 'Point a finger gun at the screen and hold it still a moment: that spot is the middle. Drop your thumb to fire. Lower your hand to holster. Space re-centres.';
  } catch (err) {
    setGunChoice(false);
    $('webcam-note').textContent = `No webcam finger gun here (${err?.name === 'NotAllowedError' ? 'camera permission was refused' : 'the camera or the hand tracker did not start'}). Mouse and touch still work.`;
  }
});

function renderLobby() {
  const s = state;
  $('lobby-code').textContent = s.code;
  $('invite-link').value = `${location.origin}/?room=${s.code}`;
  const list = $('players');
  list.textContent = '';
  for (let seat = 0; seat < 2; ++seat) {
    const p = s.players.find((q) => q.seat === seat);
    const li = document.createElement('li');
    if (!p) {
      li.className = 'empty';
      li.textContent = 'Waiting for a second player…';
    } else {
      const n = document.createElement('span');
      n.textContent = p.name;
      li.append(n);
      if (p.id === you) { const t = document.createElement('span'); t.className = 'tag you'; t.textContent = 'YOU'; li.append(t); }
      if (p.id === s.hostId) { const t = document.createElement('span'); t.className = 'tag'; t.textContent = 'HOST'; li.append(t); }
      if (!p.connected) { const t = document.createElement('span'); t.className = 'off'; t.textContent = 'reconnecting…'; li.append(t); }
    }
    list.append(li);
  }
  const ready = s.players.length === 2 && s.players.every((p) => p.connected);
  const host = s.hostId === you;
  $('start').classList.toggle('hidden', !host);
  $('start').disabled = !ready;
  $('lobby-wait').textContent = host ? (ready ? 'Both here. Start when you are ready.' : 'Send your partner the link or the code.') : (ready ? 'Waiting for the host to start the duel…' : 'Waiting for the other player…');
}

// ---------------------------------------------------------------- the duel

function renderHud() {
  const s = state, m = me(), t = them();
  $('score').textContent = `YOU ${m?.wins ?? 0} – ${t?.wins ?? 0} THEM`;
  $('names').textContent = t ? `vs ${t.name}` : '';
  const hats = (p) => Array.from({ length: RULES.hats }, (_, i) => `<span class="${i < (p?.hats ?? 0) ? '' : 'lost'}">${HAT}</span>`).join('');
  $('my-hats').innerHTML = hats(m);
  $('their-hats').innerHTML = hats(t);
  $('ammo').textContent = '▮'.repeat(m?.ammo ?? 0);

  const prompt = $('prompt'), sub = $('subprompt');
  prompt.classList.toggle('draw', s.step === 'draw');
  const webcam = gun.on;
  switch (s.step) {
    case 'intro':
      prompt.textContent = '1 v 1';
      sub.textContent = 'First to hit the other wins the round. Shoot before DRAW! and you lose it. Three hats each.';
      break;
    case 'holster':
      prompt.textContent = 'HOLSTER';
      sub.textContent = webcam ? 'drop your gun hand to your hip' : 'hands off… wait for it';
      break;
    case 'wait':
      prompt.textContent = 'WAIT FOR IT…';
      sub.textContent = 'shooting now loses the round';
      break;
    case 'draw':
      prompt.textContent = s.banner ? '' : 'DRAW!';
      sub.textContent = '';
      break;
    case 'paused': {
      const left = Math.max(0, Math.ceil((s.stepEnds - net.serverNow()) / 1000));
      prompt.textContent = 'PAUSED';
      sub.textContent = `waiting for them to come back: ${left}s`;
      break;
    }
    default:
      prompt.textContent = '';
      sub.textContent = '';
  }
  const b = s.banner;
  $('banner').classList.toggle('on', Boolean(b));
  if (b) {
    // The server names players; each screen says it to its own player.
    const mine = b.winnerId === you, loser = b.loserId === you;
    let head = b.text, small = b.sub || '';
    if (b.foul) { head = loser ? 'YOU DREW EARLY' : 'THEY DREW EARLY'; small = loser ? 'the round goes to them' : 'the round is yours'; }
    else if (b.winnerId) { head = mine ? 'YOU WIN THE ROUND' : 'THEY WIN THE ROUND'; small = `${b.ms} ms`; }
    $('banner').querySelector('b').textContent = head;
    $('banner').querySelector('small').textContent = small;
  }
}

function renderResults() {
  const s = state, m = me(), t = them();
  const won = s.result?.winnerId === you;
  $('verdict').textContent = won ? 'YOU WIN' : 'YOU LOSE';
  $('final').textContent = `${m?.wins ?? 0} – ${t?.wins ?? 0}`;
  $('reason').textContent = s.result?.reason === 'disconnect' ? (won ? 'They did not come back.' : 'You were gone too long.')
    : s.result?.reason === 'left' ? 'They left the duel.' : '';
  const ms = (v) => (v >= 0 ? `${v} ms` : '–');
  const acc = (p) => (p?.shots ? `${Math.round((100 * p.hits) / p.shots)}%` : '–');
  const rows = [
    ['Rounds won', m?.wins ?? 0, t?.wins ?? 0],
    ['Best draw', ms(m?.best ?? -1), ms(t?.best ?? -1)],
    ['Drew early', m?.fouls ?? 0, t?.fouls ?? 0],
    ['Shots', m?.shots ?? 0, t?.shots ?? 0],
    ['Accuracy', acc(m), acc(t)],
  ];
  const body = $('stats');
  body.textContent = '';
  for (const r of rows) {
    const tr = document.createElement('tr');
    for (const v of r) { const td = document.createElement('td'); td.textContent = String(v); tr.append(td); }
    body.append(tr);
  }
  const canRematch = s.players.length === 2 && s.players.every((p) => p.connected);
  $('rematch').disabled = !canRematch || m?.rematch;
  $('rematch-wait').textContent = !canRematch ? (s.players.length < 2 ? 'Your opponent left. Wait in the lobby for a new one.' : 'Waiting for them to reconnect…')
    : m?.rematch ? 'Waiting for them…' : t?.rematch ? 'They want a rematch.' : '';
}

function onState(s) {
  const first = !state;
  state = s;
  const m = me();
  world.mySeat = m ? m.seat : -1;

  // Moments: a sound and a pose for each step of the round, once.
  if (s.phase === 'match' && (s.step !== lastStep || s.round !== lastRound)) {
    if (s.step === 'wait') { sound.play('heartbeat', 0.9); world.gunAnim('fp_holster'); }
    if (s.step === 'draw') { drawShownAt = performance.now(); sound.play('whistle', 0.9); world.gunAnim('fp_draw'); }
    if (s.step === 'intro' || (s.step === 'holster' && lastStep === 'point')) { world.gunAnim('fp_reload'); sound.play('reload', 0.5); }
  }
  const b = s.banner;
  const bannerKey = b ? `${b.text}|${b.until}` : null;
  if (b && bannerKey !== lastBanner && b.loserId) {
    const loserSeat = s.players.find((p) => p.id === b.loserId)?.seat;
    if (loserSeat !== undefined) { world.hatOff(loserSeat); if (loserSeat !== m?.seat) { world.playOnce(loserSeat, 'hit', 700); } }
    if (b.loserId === you) { $('flash').classList.add('on'); setTimeout(() => $('flash').classList.remove('on'), 120); sound.play('hurt'); navigator.vibrate?.(200); }
    else { sound.play('grunt', 0.8); }
  }
  lastBanner = bannerKey;
  if (s.phase === 'results' && lastStep !== 'results') { sound.play('bell', s.result?.winnerId === you ? 0.8 : 0.3, s.result?.winnerId === you ? 1.0 : 0.7); }
  lastStep = s.phase === 'results' ? 'results' : s.step;
  lastRound = s.round;

  if (s.phase === 'lobby') { show('lobby'); renderLobby(); }
  else if (s.phase === 'match') { show('hud'); renderHud(); }
  else if (s.phase === 'results') { show('results'); renderResults(); }
  if (first) { history.replaceState(null, '', `/?room=${s.code}`); }
}

function shoot(aim, kind, at = performance.now()) {
  sound.unlock();
  if (!state || state.phase !== 'match') { return; }
  const m = me(), t = them();
  if (!m || !t) { return; }
  if (state.step !== 'wait' && state.step !== 'draw') { return; }      // holstered: nothing to fire yet
  if (m.ammo <= 0) { sound.play('dry'); return; }
  const ray = world.rayAt(aim.x, aim.y);
  // A webcam shot counts from when the thumb started to drop, not from when the tracker was sure of it.
  const react = state.step === 'draw' && drawShownAt ? Math.max(0, Math.round(at - drawShownAt)) : null;
  net.send({ t: 'shot', o: ray.o, d: ray.d, react });
  // Seen and heard at once; the server decides what it hit.
  const tp = world.smooth[t.seat];
  const res = hitTest(ray.o, ray.d, pose(t.seat, tp.lean, tp.duck));
  world.shotFx(m.seat, res.point);
  sound.play('shot_player');
  if (kind !== 'mouse') { tapShownUntil = performance.now() + 500; }
}

function onShot(msg) {
  const shooter = state?.players.find((p) => p.id === msg.by);
  if (!shooter) { return; }
  if (msg.by === you) { if (msg.hit) { crosshairHitUntil = performance.now() + 250; } return; }
  world.shotFx(shooter.seat, msg.e);
  sound.play('shot_enemy', 0.9);
  if (!msg.hit) { sound.play('whiz', 0.6, 0.9 + Math.random() * 0.2); }
}

// ---------------------------------------------------------------- network

net = new Net({
  joined: (msg) => { you = msg.id; },
  state: onState,
  shot: onShot,
  error: (msg) => {
    if (!state) { show('home'); $('home-error').textContent = msg.message; }
    else { $('lobby-error').textContent = msg.message; toast(msg.message); }
  },
  status: (s) => {
    const el = $('netstate');
    el.classList.toggle('hidden', s === 'ok' || s === 'connecting');
    el.textContent = 'Reconnecting…';
    if (s === 'reconnecting') { toast('Connection lost. Reconnecting…'); }
  },
  replaced: () => { toast('This duel is open in another tab.'); state = null; show('home'); },
});

// Back after a refresh: this tab still holds its seat's token, so it simply rejoins.
if (ROOM_CODE.test(invited)) {
  try { if (sessionStorage.getItem(`rdd:token:${invited}`)) { net.join(invited, store.get('rdd:name')); } } catch { /* private mode */ }
}

// ---------------------------------------------------------------- every frame

$('touchpad').classList.toggle('hidden', !controls.touch);
$('keys').textContent = controls.touch ? 'tap them to shoot · hold the arrows to lean and duck' : 'click to shoot · hold A / D to lean · S to duck';

let last = performance.now();
let inputSentAt = 0;
let sent = '';
function frame(now) {
  const dt = Math.min(0.05, (now - last) / 1000);
  last = now;
  controls.tick(dt);
  const aim = gun.on && gun.tracking ? gun.aim : controls.aim;
  const input = {
    lean: Math.round(controls.lean * 100) / 100,
    duck: Math.round(controls.duck * 100) / 100,
    holster: gun.on ? gun.holster : true,
    aiming: gun.on ? !gun.holster : true,
    webcam: gun.on,
  };
  const key = JSON.stringify(input);
  if (state && key !== sent && now - inputSentAt > 66) { net.send({ t: 'input', ...input }); sent = key; inputSentAt = now; }

  // Crosshair: always for a mouse or the webcam; on a touch screen, just where you tapped.
  const ch = $('crosshair');
  const showCh = state?.phase === 'match' && (!controls.touch || gun.on || now < tapShownUntil);
  ch.classList.toggle('hidden', !showCh);
  if (showCh) {
    ch.style.left = `${(aim.x + 1) * 50}%`;
    ch.style.top = `${(1 - aim.y) * 50}%`;
    ch.classList.toggle('hit', now < crosshairHitUntil);
    ch.classList.toggle('off', state.step !== 'draw' && state.step !== 'wait');
  }
  if (state?.phase === 'match' && state.step === 'paused') { renderHud(); }

  world.update(dt, {
    serverNow: net.serverNow(),
    seed: state?.seed,
    rideStart: state?.rideStart,
    mySeat: world.mySeat,
    players: state?.players,
    phase: state?.phase ?? 'home',
    step: state?.step,
    me: { lean: controls.lean, duck: controls.duck, aim },
  });
  requestAnimationFrame(frame);
}

world.load((p) => { $('load-bar').value = p; }).then(() => {
  $('loading').classList.add('hidden');
  requestAnimationFrame(frame);
}).catch((err) => {
  $('loading').querySelector('p').textContent = 'The train could not load. Check your connection and reload.';
  console.error(err);
});
