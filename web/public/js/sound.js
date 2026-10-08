// The game's own sounds (unreal/FingerGunGame/Scripts/audio_src), played through Web Audio. Browsers only allow sound
// after a tap or a click, so unlock() is called from the first one.

const NAMES = ['shot_player', 'shot_enemy', 'whistle', 'heartbeat', 'hurt', 'grunt', 'train_loop', 'dry', 'reload', 'bell', 'whiz', 'thud'];

export class Sound {
  constructor() {
    this.ctx = null;
    this.buffers = new Map();
    this.loop = null;
  }

  unlock() {
    if (this.ctx) { if (this.ctx.state === 'suspended') { this.ctx.resume(); } return; }
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) { return; }
    this.ctx = new Ctx();
    this.master = this.ctx.createGain();
    this.master.gain.value = 0.8;
    this.master.connect(this.ctx.destination);
    for (const name of NAMES) {
      fetch(`/audio/${name}.m4a`).then((r) => r.arrayBuffer()).then((b) => this.ctx.decodeAudioData(b)).then((buf) => {
        this.buffers.set(name, buf);
        if (name === 'train_loop') { this.startLoop(); }
      }).catch(() => {});
    }
  }

  play(name, volume = 1, rate = 1) {
    const buf = this.ctx && this.buffers.get(name);
    if (!buf) { return; }
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.playbackRate.value = rate;
    const gain = this.ctx.createGain();
    gain.gain.value = volume;
    src.connect(gain).connect(this.master);
    src.start();
  }

  startLoop() {
    if (this.loop || !this.buffers.get('train_loop')) { return; }
    const src = this.ctx.createBufferSource();
    src.buffer = this.buffers.get('train_loop');
    src.loop = true;
    this.loopGain = this.ctx.createGain();
    this.loopGain.gain.value = 0.18;
    src.connect(this.loopGain).connect(this.master);
    src.start();
    this.loop = src;
  }
}
