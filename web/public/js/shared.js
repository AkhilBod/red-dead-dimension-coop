// Shared by the server (which judges every shot) and the browser (which draws it), so both agree on where a cowboy is.
// Units are metres in "roof space": the train never moves, x runs along it (+x towards the locomotive), y is up from the
// boxcar's roof walkway, z is sideways.

export const RULES = {
  hats: 3,                  // lose them all and you have lost the duel
  ammo: 6,                  // per round; reloaded between rounds
  introMs: 3500,
  holsterMs: 600,           // both holstered this long and the wait begins
  holsterMaxMs: 5000,       // or this long anyway
  waitMinMs: 1800,          // WAIT FOR IT... then DRAW! at a random moment
  waitMaxMs: 3400,
  drawMaxMs: 6000,          // nobody hit by then: the round is replayed
  pointMs: 3200,            // the round's result on screen
  pauseMs: 30000,           // a player who drops mid-duel has this long to come back
  graceMs: 30000,           // and outside a duel (a refresh, a phone gone to sleep) keeps their place this long
  shotGapMs: 220,           // the fastest a revolver fires
  trainSpeed: 14,           // m/s, the train rolling on through the desert
};

// Two cowboys on one boxcar roof, seven metres apart, facing each other.
export const SEATS = [
  { x: 3.5, facing: -1 },   // seat 0 looks back down the train
  { x: -3.5, facing: 1 },   // seat 1 looks up it, at the locomotive
];
export const EYE_STAND = 1.62;
export const EYE_DUCK = 1.02;
export const LEAN_M = 0.6;

const add = (a, b) => [a[0] + b[0], a[1] + b[1], a[2] + b[2]];
const sub = (a, b) => [a[0] - b[0], a[1] - b[1], a[2] - b[2]];
const scale = (a, s) => [a[0] * s, a[1] * s, a[2] * s];
const dot = (a, b) => a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
const len = (a) => Math.sqrt(dot(a, a));
export const normalize = (a) => { const l = len(a) || 1; return scale(a, 1 / l); };

const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** Where a seated player is, from their lean (-1..1, to their own right) and duck (0..1). */
export function pose(seat, lean = 0, duck = 0) {
  const s = SEATS[seat];
  const right = [0, 0, s.facing];                     // facing +x, right is +z; facing -x, right is -z
  lean = clamp(Number(lean) || 0, -1, 1);
  duck = clamp(Number(duck) || 0, 0, 1);
  const eyeY = EYE_STAND + (EYE_DUCK - EYE_STAND) * duck;
  const eye = add([s.x, eyeY, 0], scale(right, lean * LEAN_M));
  return {
    eye,
    head: add(eye, [0, 0.07, 0]),
    // the body leans from the hips: shoulders go most of the way, hips stay put
    hip: add([s.x, 0.95 - 0.35 * duck, 0], scale(right, lean * LEAN_M * 0.2)),
    shoulder: add([s.x, eyeY - 0.25, 0], scale(right, lean * LEAN_M * 0.85)),
    forward: [s.facing, 0, 0],
    right,
  };
}

function raySphere(o, d, c, r) {
  const oc = sub(o, c);
  const b = dot(oc, d);
  const q = dot(oc, oc) - r * r;
  const h = b * b - q;
  if (h < 0) { return -1; }
  const t = -b - Math.sqrt(h);
  return t > 0 ? t : -1;
}

/** Closest distance between a ray and a segment, and how far along the ray. */
function raySegment(o, d, a, b) {
  const u = sub(b, a);
  const w = sub(o, a);
  const A = dot(d, d), B = dot(d, u), C = dot(u, u), D = dot(d, w), E = dot(u, w);
  const den = A * C - B * B;
  let t = den > 1e-9 ? (B * E - C * D) / den : 0;
  let s = den > 1e-9 ? (A * E - B * D) / den : 0;
  s = clamp(s, 0, 1);
  t = Math.max(0, (B * s - D) / A);
  const p = add(o, scale(d, t));
  const q = add(a, scale(u, s));
  return { dist: len(sub(p, q)), t };
}

/**
 * Does a shot from origin along dir hit the cowboy at this pose? A little extra width for webcam aim (slack), much
 * less than against the bandits of the solo game: leaning out of the line of fire works.
 */
export function hitTest(origin, dir, p, slack = 0.08) {
  const d = normalize(dir);
  const tHead = raySphere(origin, d, p.head, 0.13 + slack);
  if (tHead > 0) { return { hit: true, part: 'head', point: add(origin, scale(d, tHead)) }; }
  const body = raySegment(origin, d, p.hip, p.shoulder);
  if (body.dist < 0.22 + slack && body.t > 0) { return { hit: true, part: 'body', point: add(origin, scale(d, body.t)) }; }
  return { hit: false, point: add(origin, scale(d, 60)) };
}

export const ROOM_CODE = /^[A-HJ-NP-Z]{4}$/;     // no I or O: they read as 1 and 0
export const ROOM_LETTERS = 'ABCDEFGHJKLMNPQRSTUVWXYZ';
