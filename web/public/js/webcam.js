// The finger gun, in the browser: MediaPipe's hand landmarker on the webcam, and the tracker's own logic
// (fingergun.js) on top: aim from the fingertip's travel, fire on a thumb drop with the aim rewound to before the
// flinch, holster when the gun hand goes down.
import { GunTracker } from './fingergun.js';

const VISION = 'https://cdn.jsdelivr.net/npm/@mediapipe/tasks-vision@0.10.14';
const MODEL = 'https://storage.googleapis.com/mediapipe-models/hand_landmarker/hand_landmarker/float16/1/hand_landmarker.task';

export class FingerGun {
  constructor() {
    this.on = false;
    this.aim = { x: 0, y: 0 };      // ndc
    this.tracking = false;          // a gun hand is in view: the crosshair follows it
    this.holster = true;
    this.status = '';
    this.bangUntil = 0;
    this.onFire = () => {};
  }

  async start(video, dots) {
    const { FilesetResolver, HandLandmarker } = await import(`${VISION}/vision_bundle.mjs`);
    const files = await FilesetResolver.forVisionTasks(`${VISION}/wasm`);
    const make = (delegate) => HandLandmarker.createFromOptions(files, {
      baseOptions: { modelAssetPath: MODEL, delegate },
      runningMode: 'VIDEO',
      numHands: 2,                  // the tracker picks the gun hand; the other one cannot steal the aim
    });
    this.hands = await make('GPU').catch(() => make('CPU'));
    this.stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: 'user', width: 640, height: 480 }, audio: false });
    video.srcObject = this.stream;
    await video.play();
    this.video = video;
    this.dots = dots;
    this.tracker = new GunTracker(innerWidth / innerHeight);
    this.on = true;
    this.lastTime = -1;
    const loop = () => {
      if (!this.on) { return; }
      if (video.currentTime !== this.lastTime && video.videoWidth) {
        this.lastTime = video.currentTime;
        const now = performance.now();
        this.process(this.hands.detectForVideo(video, now), now, video.videoWidth / video.videoHeight);
      }
      requestAnimationFrame(loop);
    };
    loop();
  }

  stop() {
    this.on = false;
    this.stream?.getTracks().forEach((t) => t.stop());
    this.holster = true;
    this.tracking = false;
  }

  recenter() { this.tracker?.recenter(); }

  process(res, now, aspect) {
    // Mirrored like the tracker's camera picture: move your hand right and x grows.
    const hands = (res.landmarks || []).map((lm, i) => ({
      pts: lm.map((p) => [1 - p.x, p.y, p.z]),
      world: res.worldLandmarks?.[i]?.map((p) => [-p.x, p.y, p.z]) ?? null,
      score: res.handedness?.[i]?.[0]?.score ?? 1,
    }));
    this.tracker.screenAspect = innerWidth / innerHeight;
    const s = this.tracker.update(now / 1000, hands, aspect);
    this.tracking = s.aimValid;
    this.holster = s.holstered;
    if (s.aimValid) { this.aim = { x: s.aim[0] * 2 - 1, y: 1 - s.aim[1] * 2 }; }
    if (s.fire) {
      this.bangUntil = now + 400;
      this.onFire({ x: s.fire.aim[0] * 2 - 1, y: 1 - s.fire.aim[1] * 2 }, s.fire.onset * 1000);
    }
    this.status = now < this.bangUntil ? 'BANG' : s.holstered ? 'holstered' : s.aimValid ? 'aiming' : hands.length ? 'make a finger gun' : 'no hand';
    this.draw(res.landmarks || [], s);
  }

  draw(all, s) {
    const g = this.dots?.getContext('2d');
    if (!g) { return; }
    this.dots.width = 168;
    this.dots.height = 126;
    g.clearRect(0, 0, 168, 126);
    // The canvas is mirrored by CSS, like the video under it, so the dots go on in camera coordinates.
    g.fillStyle = s.aimValid ? '#f2ad38' : '#eed9ad';
    for (const lm of all) {
      for (const i of [0, 4, 5, 8]) { g.beginPath(); g.arc(lm[i].x * 168, lm[i].y * 126, i === 8 ? 5 : 3.5, 0, Math.PI * 2); g.fill(); }
    }
    g.save();
    g.scale(-1, 1);                  // the label reads the right way round
    g.font = '700 13px system-ui, sans-serif';
    g.fillStyle = 'rgba(0, 0, 0, 0.55)';
    g.fillRect(-168, 104, 168, 22);
    g.fillStyle = this.status === 'BANG' ? '#ff7a59' : '#eed9ad';
    g.fillText(this.status.toUpperCase(), -162, 120);
    g.restore();
  }
}
