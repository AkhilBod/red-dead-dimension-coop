// Mouse, keys and touch. Aim is in normalised device coordinates (-1..1, +y up), as three.js wants for a ray.

export class Controls {
  constructor(canvas, pad, onShoot) {
    this.aim = { x: 0, y: 0 };
    this.lean = 0;
    this.duck = 0;
    this.keys = new Set();
    this.hold = { left: false, right: false, duck: false };
    this.touch = matchMedia('(pointer: coarse)').matches;
    this.onShoot = onShoot;
    this.onRecenter = () => {};

    const ndc = (e) => ({ x: (e.clientX / innerWidth) * 2 - 1, y: -((e.clientY / innerHeight) * 2 - 1) });
    canvas.addEventListener('pointermove', (e) => { if (e.pointerType === 'mouse') { this.aim = ndc(e); } });
    canvas.addEventListener('pointerdown', (e) => {
      this.aim = ndc(e);
      this.onShoot(this.aim, e.pointerType);
    });
    canvas.addEventListener('contextmenu', (e) => e.preventDefault());

    addEventListener('keydown', (e) => {
      if (e.target instanceof HTMLInputElement) { return; }
      this.keys.add(e.code);
      if (e.code === 'Space') { e.preventDefault(); this.onRecenter(); }
    });
    addEventListener('keyup', (e) => this.keys.delete(e.code));
    addEventListener('blur', () => { this.keys.clear(); for (const k in this.hold) { this.hold[k] = false; } });

    // Hold-to-lean and hold-to-duck buttons on a touch screen.
    for (const b of pad.querySelectorAll('[data-hold]')) {
      const key = b.dataset.hold;
      const set = (on) => (e) => { e.preventDefault(); this.hold[key] = on; b.classList.toggle('down', on); };
      b.addEventListener('pointerdown', set(true));
      b.addEventListener('pointerup', set(false));
      b.addEventListener('pointercancel', set(false));
      b.addEventListener('pointerleave', set(false));
    }
  }

  tick(dt) {
    const k = this.keys;
    const left = k.has('KeyA') || k.has('ArrowLeft') || this.hold.left;
    const right = k.has('KeyD') || k.has('ArrowRight') || this.hold.right;
    const down = k.has('KeyS') || k.has('ArrowDown') || this.hold.duck;
    const leanTo = (right ? 1 : 0) - (left ? 1 : 0);
    this.lean += (leanTo - this.lean) * (1 - Math.exp(-dt * 9));
    this.duck += ((down ? 1 : 0) - this.duck) * (1 - Math.exp(-dt * 11));
  }
}
