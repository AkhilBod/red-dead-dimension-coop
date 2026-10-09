// The finger gun's logic, ported from the tracker (tracker/pipeline.py, trigger.py, aim.py, hand_features.py), whose
// numbers were set on recorded sessions and live play. Pure: landmarks in, aim and shots out, so the tracker's
// recordings (python run.py --record) can be replayed through it in Node. Units as in the tracker: m = real metres, H = image heights, s = seconds.
// The browser has no body model, so this is the tracker's no-body path: no chest anchor, a fixed holster line.

export const CFG = {
  aimSpanM: 0.46,              // metres of fingertip travel that cross the screen's long side
  aimEdgePullRate: 0.4,        // screens per second that a hand held past an edge pulls the centre along
  aimSettleSpeed: 0.25,        // m/s: slower than this for aimSettleS and where it points is the middle of the screen
  aimSettleS: 0.2,
  aimSettleMaxS: 0.8,          // never settles (firing at once): take wherever it is by then
  aimRecenterAfterS: 0.6,      // gun hand gone this long: learn the centre again when it comes back
  aimPushMaxSpeed: 1.2,        // m/s: faster is a tracking jump, not a hand held past the edge
  aimMinCutoff: 1.0, aimBeta: 20, aimDCutoff: 3.0,
  aimLeadS: 0.03, aimLeadFrom: 0.08, aimLeadFull: 0.35,
  aimHoldS: 0.25,              // the crosshair stays live through dropouts this short
  handScaleWindowS: 0.7,
  gunLockJump: 1.5,            // hand lengths the gun hand may jump between frames and stay the gun
  gunUnlockS: 0.4,
  handDedupe: 0.5,
  holsterY: 0.85,              // H: a wrist below this line is down at the hip
  holsterDelayS: 0.25,
  openFingerExt: 0.85, openThumbExt: 0.78, openVisibleFrac: 0.6,
  poseOnFrames: 2, poseOffS: 0.3,
  thumbMinCocked: 0.42,        // a peak lower than this is noise around "down"
  thumbDropFrac: 0.30,         // fire when the thumb falls this far below its recent peak
  thumbRearmFrac: 0.30,
  thumbWindowS: 0.4,
  thumbConfirmFrames: 2,
  thumbMaxSpeed: 0.35,         // m/s: thumb landmarks are garbage while the hand moves
  thumbMaxSwing: 0.025,        // m of hand rotation within thumbSwingS that still counts as steady
  thumbSwingS: 0.2,
  flickBaselineS: 0.5,
  fireCooldownS: 0.25,
  rewindMarginS: 0.04, rewindWindowS: 0.15, rewindMaxS: 0.6, rewindStillSpeed: 0.12,
  triggerGapResetS: 0.25,
};

const WRIST = 0, THUMB_TIP = 4, INDEX_MCP = 5, INDEX_PIP = 6, INDEX_TIP = 8, MIDDLE_MCP = 9;
const FINGERS = [[5, 6, 7, 8], [9, 10, 11, 12], [13, 14, 15, 16], [17, 18, 19, 20]];
const THUMB = [1, 2, 3, 4];
const PALM = [0, 5, 9, 13, 17];
// Real lengths of the rigid palm bones (medians of MediaPipe's metric landmarks). Some bone is always roughly side-on,
// so the largest image/real ratio is the hand's image scale: hand travel in real centimetres at any distance.
const LONG_BONES = [[0, 5, 0.0965], [0, 9, 0.0916], [0, 13, 0.0887], [0, 17, 0.0765], [5, 17, 0.062]];
const PALM_LENGTH_M = 0.0916;
const HAND_LENGTH_M = 0.19;

const d2 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1]);
const d3 = (a, b) => Math.hypot(a[0] - b[0], a[1] - b[1], a[2] - b[2]);
const chainLength = (p, c) => d3(p[c[0]], p[c[1]]) + d3(p[c[1]], p[c[2]]) + d3(p[c[2]], p[c[3]]);
const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
const median = (xs) => {
  const s = [...xs].sort((a, b) => a - b), m = (s.length - 1) / 2;
  return (s[Math.floor(m)] + s[Math.ceil(m)]) / 2;
};

function imageScale(p2) {
  return Math.max(...LONG_BONES.map(([i, j, len]) => d2(p2[i], p2[j]) / len));
}

/** All five fingers straight AND visibly long: a fist pointed at the camera reads as straight fingers otherwise. */
function isOpenPalm(w, p2, scale) {
  if (!w) { return false; }
  for (const c of FINGERS) {
    const len = chainLength(w, c);
    if (len < 1e-6 || d3(w[c[3]], w[c[0]]) / len < CFG.openFingerExt) { return false; }
    if (d2(p2[c[3]], p2[c[0]]) < CFG.openVisibleFrac * len * scale) { return false; }
  }
  const thumb = chainLength(w, THUMB);
  return thumb > 1e-6 && d3(w[4], w[1]) / thumb > CFG.openThumbExt;
}

/** Thumb tip to index knuckle and PIP, in palm lengths, from the metric landmarks: high = cocked, low = hammer down. */
function thumbFeature(w) {
  if (!w || d3(w[MIDDLE_MCP], w[WRIST]) < 0.06) { return null; }
  return (d3(w[THUMB_TIP], w[INDEX_MCP]) + d3(w[THUMB_TIP], w[INDEX_PIP])) / (2 * PALM_LENGTH_M);
}

class Window {
  constructor(seconds) { this.seconds = seconds; this.buf = []; }
  clear() { this.buf = []; }
  push(t, v) {
    this.buf.push([t, v]);
    while (this.buf[0][0] < t - this.seconds) { this.buf.shift(); }
  }
  values() { return this.buf.map((e) => e[1]); }
}

class OneEuro {
  constructor() { this.reset(); }
  reset() { this.x = null; this.dx = null; this.t = null; }
  filter(x, t) {
    const alpha = (cutoff, dt) => 1 / (1 + 1 / (2 * Math.PI * cutoff) / dt);
    if (this.x === null || t - this.t > 0.5) { this.x = x; this.dx = [0, 0]; this.t = t; return x; }
    const dt = t - this.t;
    if (dt <= 0) { return this.x; }
    const ad = alpha(CFG.aimDCutoff, dt);
    this.dx = this.dx.map((d, i) => ad * ((x[i] - this.x[i]) / dt) + (1 - ad) * d);
    const a = alpha(CFG.aimMinCutoff + CFG.aimBeta * Math.hypot(...this.dx), dt);
    this.x = this.x.map((v, i) => a * x[i] + (1 - a) * v);
    this.t = t;
    return this.x;
  }
}

/** Hammer drop: the thumb falls by a share of its own recent peak. Relative, so it suits every hand and camera angle. */
class ThumbTrigger {
  constructor() { this.window = new Window(CFG.thumbWindowS); this.reset(); }
  reset() { this.window.clear(); this.armed = true; this.trough = Infinity; this.below = 0; this.lastT = null; this.f = null; }
  update(t, f, steady) {
    if (this.lastT !== null && t - this.lastT > CFG.triggerGapResetS) { this.reset(); }
    this.lastT = t;
    this.f = f;
    if (f === null || !steady) { this.below = 0; return null; }      // skipped, not reset: a real pull often jerks the hand
    this.window.push(t, f);
    if (!this.armed) {
      this.trough = Math.min(this.trough, f);
      if (f >= this.trough * (1 + CFG.thumbRearmFrac) && f >= CFG.thumbMinCocked) { this.armed = true; }
      return null;
    }
    const peak = Math.max(...this.window.values());
    const fell = peak >= CFG.thumbMinCocked && f <= peak * (1 - CFG.thumbDropFrac);
    this.below = fell ? this.below + 1 : 0;
    if (this.below < CFG.thumbConfirmFrames) { return null; }
    let onset = this.window.buf[0][0];
    for (let i = this.window.buf.length - 1; i >= 0; i--) {
      if (this.window.buf[i][1] >= peak * 0.92) { onset = this.window.buf[i][0]; break; }      // last moment the thumb was up
    }
    this.armed = false;
    this.trough = f;
    this.below = 0;
    this.window.clear();
    return onset;
  }
}

/** How much the hand has rotated lately (fingertip height over the wrist): the thumb trigger freezes while it swings. */
class Swing {
  constructor() { this.window = new Window(CFG.flickBaselineS); this.lastT = null; }
  reset() { this.window.clear(); this.lastT = null; }
  update(t, rise) {
    if (this.lastT !== null && t - this.lastT > CFG.triggerGapResetS) { this.window.clear(); }
    this.lastT = t;
    this.window.push(t, rise);
  }
  over(seconds) {
    if (!this.window.buf.length) { return 0; }
    const now = this.window.buf[this.window.buf.length - 1][0];
    const recent = this.window.buf.filter(([ts]) => ts >= now - seconds).map((e) => e[1]);
    return Math.max(...recent) - Math.min(...recent);
  }
}

/** Recent raw aim, so a shot uses where the hand was held BEFORE the trigger motion moved it. */
class AimHistory {
  constructor() { this.buf = []; }
  clear() { this.buf = []; }
  push(t, raw) {
    this.buf.push([t, raw]);
    while (this.buf[0][0] < t - 1) { this.buf.shift(); }
  }
  at(time) {
    if (!this.buf.length) { return null; }
    for (let i = this.buf.length - 1; i >= 0; i--) { if (this.buf[i][0] <= time) { return this.buf[i][1]; } }
    return this.buf[0][1];
  }
  /** Held still: the median over the window. Sweeping: the straight line through it, read at readAt. */
  settledBefore(time, window, stillSpeed, readAt) {
    const pts = this.buf.filter(([t]) => time - window <= t && t <= time);
    if (pts.length < 2) { return this.at(time); }
    const ts = pts.map((p) => p[0]);
    const axis = (i) => pts.map((p) => p[1][i]);
    const med = [median(axis(0)), median(axis(1))];
    if (pts.length < 3 || Math.max(...ts) - Math.min(...ts) < 1e-3) { return med; }
    const last = ts[ts.length - 1];
    const xs = ts.map((t) => t - last);
    const mx = xs.reduce((a, b) => a + b, 0) / xs.length;
    const vx = xs.reduce((a, x) => a + (x - mx) ** 2, 0);
    const fit = [0, 1].map((i) => {
      const ys = axis(i), my = ys.reduce((a, b) => a + b, 0) / ys.length;
      const slope = xs.reduce((a, x, k) => a + (x - mx) * (ys[k] - my), 0) / vx;
      return [slope, my - slope * mx];
    });
    if (Math.hypot(fit[0][0], fit[1][0]) < stillSpeed) { return med; }
    return fit.map(([slope, icpt]) => icpt + slope * (readAt - last));
  }
}

/**
 * Feed it every camera frame: update(t seconds, hands, aspect). hands = [{ pts: 21 x [x, y, z] in 0..1 of a MIRRORED
 * picture (move right, x grows), world: 21 x [x, y, z] metres or null, score }]. It returns
 *   { aim: [x, y] 0..1 (y down), aimValid, gunPose, holstered, fire: { aim, onset } | null }.
 */
export class GunTracker {
  constructor(screenAspect = 16 / 9) {
    this.screenAspect = screenAspect;
    this.filter = new OneEuro();
    this.scales = new Window(CFG.handScaleWindowS);
    this.thumb = new ThumbTrigger();
    this.swing = new Swing();
    this.history = new AimHistory();
    this.center = null;
    this.aim = [0.5, 0.5];
    this.gunPos = null;
    this.gunCenter = null;
    this.gunSeenT = -1e9;
    this.handSpeed = 0;
    this.refTip = null;
    this.stillSince = null;
    this.centering = [];
    this.lockT = null;
    this.recenterAim = false;
    this.lastFireT = -1e9;
    this.gunPose = false;
    this.poseFrames = 0;
    this.poseTrueT = -1e9;
    this.downSince = null;
    this.thumbValue = null;
  }

  recenter() { this.recenterAim = true; }

  span() {
    // The screen's long side takes aimSpanM, so the same hand travel moves the crosshair as far either way.
    const a = this.screenAspect;
    return a >= 1 ? [CFG.aimSpanM, CFG.aimSpanM / a] : [CFG.aimSpanM * a, CFG.aimSpanM];
  }

  /** dt > 0 lets a hand held past an edge pull the centre along; dt = 0 only reads. */
  map(raw, dt = 0) {
    if (!raw || !this.center) { return [0.5, 0.5]; }
    const span = this.span();
    const s = [0, 1].map((i) => 0.5 + (raw[i] - this.center[i]) / span[i]);
    const c = s.map((v) => clamp(v, 0, 1));
    if (dt > 0) {
      const step = CFG.aimEdgePullRate * dt;
      this.center = this.center.map((v, i) => v + clamp(s[i] - c[i], -step, step) * span[i]);
    }
    return c;
  }

  dropLock() {
    this.gunPos = null;
    this.gunCenter = null;
    this.handSpeed = 0;
    this.stillSince = null;
    this.centering = [];
    this.lockT = null;
    this.filter.reset();
    this.history.clear();
    this.scales.clear();
    this.thumb.reset();
    this.swing.reset();
  }

  hands(raw, aspect) {
    const infos = [];
    for (const hand of [...raw].sort((a, b) => b.score - a.score)) {
      const p2 = hand.pts.map((p) => [p[0] * aspect, p[1]]);
      const scale = imageScale(p2);
      const center = [0, 1].map((i) => PALM.reduce((a, k) => a + p2[k][i], 0) / PALM.length);
      const world = hand.world?.length === 21 ? hand.world : null;
      const h = { world, p2, wrist: p2[WRIST], center, scale, length: HAND_LENGTH_M * scale };
      if (infos.some((k) => d2(h.center, k.center) < CFG.handDedupe * Math.max(h.length, k.length))) { continue; }
      h.open = isOpenPalm(world, p2, scale);
      h.raised = h.wrist[1] < CFG.holsterY;
      infos.push(h);
    }
    return infos;
  }

  pickGun(infos, t) {
    const raised = infos.filter((h) => h.raised);
    if (this.gunPos) {
      if (t - this.gunSeenT > CFG.gunUnlockS) {
        this.dropLock();
      } else {
        const near = raised.filter((h) => d2(h.wrist, this.gunPos) < CFG.gunLockJump * h.length);
        return near.length ? near.reduce((a, b) => (d2(a.wrist, this.gunPos) <= d2(b.wrist, this.gunPos) ? a : b)) : null;
      }
    }
    const candidates = raised.filter((h) => !h.open);
    return candidates.length ? candidates.reduce((a, b) => (b.scale > a.scale ? b : a)) : null;   // the one held out nearest
  }

  update(t, rawHands, aspect) {
    const infos = this.hands(rawHands, aspect);
    const hadLock = this.gunPos !== null;
    const gun = this.pickGun(infos, t);
    let fire = null;
    this.thumbValue = null;

    if (gun) {
      const dt = hadLock ? Math.min(0.1, t - this.gunSeenT) : 0;
      if (!this.gunPos) {
        if (t - this.gunSeenT > CFG.aimRecenterAfterS) { this.center = null; this.refTip = null; }
        this.dropLock();
      }
      // Fingertip travel in real metres from a fixed reference: image travel over the hand's own image scale.
      this.scales.push(t, gun.scale);
      const scale = median(this.scales.values());
      const tip = gun.p2[INDEX_TIP];
      if (!this.refTip) { this.refTip = tip; }
      const travel = [(tip[0] - this.refTip[0]) / scale, (tip[1] - this.refTip[1]) / scale];
      const raw = this.lead(this.filter.filter(travel, t));
      this.history.push(t, raw);

      if (this.gunCenter && t > this.gunSeenT) {
        const v = d2(gun.center, this.gunCenter) / (t - this.gunSeenT) / scale;
        this.handSpeed += (v - this.handSpeed) * 0.5;
      }
      this.gunPos = gun.wrist;
      this.gunCenter = gun.center;
      this.gunSeenT = t;

      // Where the hand comes to rest is the middle of the screen.
      if (this.recenterAim) {
        this.center = raw;
        this.recenterAim = false;
      } else if (!this.center) {
        if (this.lockT === null) { this.lockT = t; }
        if (this.handSpeed > CFG.aimSettleSpeed) {
          this.stillSince = null;
          this.centering = [];
        } else {
          this.centering.push(raw);
          if (this.stillSince === null) { this.stillSince = t; }
        }
        if (this.stillSince !== null && t - this.stillSince >= CFG.aimSettleS) {
          this.center = [0, 1].map((i) => median(this.centering.map((r) => r[i])));
        } else if (t - this.lockT >= CFG.aimSettleMaxS) {
          this.center = raw;
        }
      }
      this.aim = this.map(raw, this.handSpeed < CFG.aimPushMaxSpeed ? dt : 0);

      this.swing.update(t, (gun.p2[WRIST][1] - gun.p2[INDEX_TIP][1]) / scale);
      const steady = this.handSpeed < CFG.thumbMaxSpeed && this.swing.over(CFG.thumbSwingS) < CFG.thumbMaxSwing;
      this.thumbValue = thumbFeature(gun.world);
      const onset = this.thumb.update(t, this.thumbValue, steady);
      if (onset !== null && t - this.lastFireT >= CFG.fireCooldownS) {
        const at = Math.max(onset, t - CFG.rewindMaxS);
        const then = this.history.settledBefore(at - CFG.rewindMarginS, CFG.rewindWindowS, CFG.rewindStillSpeed, at);
        fire = { aim: this.map(then), onset: at };
        this.lastFireT = t;
      }
    }

    if (gun && !gun.open) {
      this.poseFrames += 1;
      this.poseTrueT = t;
    } else {
      this.poseFrames = 0;
    }
    if (this.poseFrames >= CFG.poseOnFrames) { this.gunPose = true; } else if (t - this.poseTrueT > CFG.poseOffS) { this.gunPose = false; }

    // Holstered = no hand up: dropped to the hip, below the holster line or out of the picture.
    const up = Boolean(gun) || infos.some((h) => h.raised);
    if (up) { this.downSince = null; } else if (this.downSince === null) { this.downSince = t; }
    const holstered = !up && t - this.downSince >= CFG.holsterDelayS;

    return { aim: this.aim, aimValid: t - this.gunSeenT <= CFG.aimHoldS, gunPose: this.gunPose, holstered, fire };
  }

  /** Where the hand is by now, not where the camera saw it a frame or two ago. */
  lead(raw) {
    const v = this.filter.dx;
    if (!v) { return raw; }
    const g = clamp((Math.hypot(...v) - CFG.aimLeadFrom) / (CFG.aimLeadFull - CFG.aimLeadFrom), 0, 1);
    return raw.map((x, i) => x + CFG.aimLeadS * g * g * (3 - 2 * g) * v[i]);
  }
}
