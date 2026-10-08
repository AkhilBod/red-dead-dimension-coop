// The finger gun, in the browser: MediaPipe's hand landmarker on the webcam, and a small port of the tracker's ideas
// (tracker/): aim from the fingertip, fire on a thumb drop with the aim rewound to before the flinch, holster when
// the gun hand goes down.

const VISION = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14';
const MODEL = 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task';
const GAIN = 2.4;                 // hand travel to screen travel: a comfortable reach covers the screen
const REWIND_MS = 110;            // where the gun pointed before the thumb came down

const dist = (a, b) => Math.hypot(a.x - b.x, a.y - b.y);

/** One Euro filter: heavy smoothing when the hand is still, almost none when it moves fast. */
class OneEuro {
  constructor(minCutoff = 1.4, beta = 0.02) { this.minCutoff = minCutoff; this.beta = beta; this.x = null; this.dx = 0; this.t = 0; }
  filter(v, t) {
    if (this.x === null) { this.x = v; this.t = t; return v; }
    const dt = Math.max(1e-3, (t - this.t) / 1000);
    this.t = t;
    const alpha = (cutoff) => 1 / (1 + 1 / (2 * Math.PI * cutoff * dt));
    const dv = (v - this.x) / dt;
    this.dx += alpha(1) * (dv - this.dx);
    this.x += alpha(this.minCutoff + this.beta * Math.abs(this.dx)) * (v - this.x);
    return this.x;
  }
}

export class FingerGun {
  constructor() {
    this.on = false;
    this.aim = { x: 0, y: 0 };      // ndc
    this.holster = true;
    this.gunPose = false;
    this.seenAt = 0;
    this.center = null;
    this.cocked = false;
    this.history = [];
    this.fx = new OneEuro();
    this.fy = new OneEuro();
    this.onFire = () => {};
  }

  async start(video, dots) {
    const { FilesetResolver, HandLandmarker } = await import(`${VISION}/vision_bundle.mjs`);
    const files = await FilesetResolver.forVisionTasks(`${VISION}/wasm`);
    this.hands = await HandLandmarker.createFromOptions(files, {
      baseOptions: { modelAssetPath: MODEL, delegate: 'GPU' },
      runningMode: 'VIDEO',
      numHands: 1,
    });
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user', width: 640, height: 480 }, audio: false });
    video.srcObject = this.stream;
    await video.play();
    this.video = video;
    this.dots = dots;
    this.on = true;
    this.lastTime = -1;
    const loop = () => {
      if (!this.on) { return; }
      if (video.currentTime !== this.lastTime) {
        this.lastTime = video.currentTime;
        const res = this.hands.detectForVideo(video, performance.now());
        this.process(res.landmarks?.[0], performance.now());
      }
      requestAnimationFrame(loop);
    };
    loop();
  }

  stop() {
    this.on = false;
    this.stream?.getTracks().forEach((t) => t.stop());
    this.holster = true;
  }

  recenter() { this.center = null; }

  process(lm, now) {
    const g = this.dots?.getContext('2d');
    if (g) { this.dots.width = 168; this.dots.height = 126; g.clearRect(0, 0, 168, 126); }
    if (!lm) {
      if (now - this.seenAt > 300) { this.holster = true; this.gunPose = false; }
      return;
    }
    this.seenAt = now;
    const palm = Math.max(0.03, dist(lm[0], lm[9]));
    // A finger gun: index out, middle, ring and little fingers curled.
    const indexOut = dist(lm[8], lm[0]) / palm > 1.45;
    const othersIn = dist(lm[12], lm[0]) / palm < 1.35 && dist(lm[16], lm[0]) / palm < 1.3;
    this.gunPose = indexOut && othersIn;
    this.holster = !this.gunPose || lm[0].y > 0.82;     // no gun, or the hand is down at the hip

    // Aim: the fingertip's travel from where it was when the gun came up (Space re-centres), mirrored like a mirror.
    const raw = { x: 1 - lm[8].x, y: lm[8].y };
    if (!this.center && this.gunPose) { this.center = { ...raw }; }
    const c = this.center || { x: 0.5, y: 0.5 };
    const sx = this.fx.filter(0.5 + (raw.x - c.x) * GAIN, now);
    const sy = this.fy.filter(0.5 + (raw.y - c.y) * GAIN, now);
    this.aim = { x: Math.max(-1, Math.min(1, sx * 2 - 1)), y: Math.max(-1, Math.min(1, -(sy * 2 - 1))) };
    this.history.push({ t: now, aim: this.aim });
    while (this.history.length && now - this.history[0].t > 400) { this.history.shift(); }

    // Trigger: the thumb comes down onto the side of the hand.
    const thumb = dist(lm[4], lm[5]) / palm;
    if (thumb > 0.55) { this.cocked = true; }
    if (this.cocked && thumb < 0.33 && this.gunPose) {
      this.cocked = false;
      const before = this.history.find((h) => now - h.t <= REWIND_MS) || { aim: this.aim };
      this.onFire(before.aim);
    }

    if (g) {
      g.fillStyle = this.gunPose ? '#f2ad38' : '#eed9ad';
      for (const i of [4, 5, 8]) { g.beginPath(); g.arc(lm[i].x * 168, lm[i].y * 126, i === 8 ? 5 : 3.5, 0, Math.PI * 2); g.fill(); }
    }
  }
}
