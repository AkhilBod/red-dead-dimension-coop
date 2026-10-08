// The train, the desert going by, the two cowboys and the effects, in three.js, from the game's own models.
// The train stands still at the origin and the world moves under it, as in the Unreal game.
import * as THREE from 'three';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';
import { MeshoptDecoder } from 'three/addons/libs/meshopt_decoder.module.js';
import { clone as cloneSkinned } from 'three/addons/utils/SkeletonUtils.js';
import { LEAN_M, RULES, SEATS, pose } from './shared.js';

const CHUNKS = ['chunk_flat_a', 'chunk_flat_b', 'chunk_cactus', 'chunk_rocky_a', 'chunk_rocky_b', 'chunk_mesa', 'chunk_fence'];
const RAIL_Y = 0.53;                 // rail top above the ground: where a car's origin sits
const CHUNK_M = 50;
const AHEAD_M = 420, BEHIND_M = 420; // one duellist looks up the line, the other back down it
const MOBILE = matchMedia('(pointer: coarse)').matches;

// The same scenery on both screens: which chunk goes where comes from the room's seed.
function pick(seed, i) {
  let x = (seed ^ Math.imul(i + 1, 2654435761)) >>> 0;
  x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0;
  return x % CHUNKS.length;
}

export class World {
  constructor(canvas) {
    this.renderer = new THREE.WebGLRenderer({ canvas, antialias: !MOBILE, powerPreference: 'high-performance' });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, MOBILE ? 1.5 : 2));
    this.renderer.outputColorSpace = THREE.SRGBColorSpace;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 1.05;
    this.renderer.shadowMap.enabled = !MOBILE;
    this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;

    this.scene = new THREE.Scene();
    const horizon = new THREE.Color('#e9b07a');
    this.scene.background = this.skyTexture();
    this.scene.fog = new THREE.Fog(horizon, 90, 430);

    this.camera = new THREE.PerspectiveCamera(70, 1, 0.05, 1500);
    this.scene.add(this.camera);

    this.scene.add(new THREE.HemisphereLight('#ffd9a8', '#5a3a22', 1.25));
    // Late afternoon, the sun off to one side so neither duellist has it in their eyes.
    this.sun = new THREE.DirectionalLight('#ffd2a0', 2.6);
    this.sun.position.set(-20, 30, 60);
    this.sun.castShadow = !MOBILE;
    this.sun.shadow.mapSize.set(2048, 2048);
    Object.assign(this.sun.shadow.camera, { left: -30, right: 30, top: 20, bottom: -20, near: 1, far: 200 });
    this.scene.add(this.sun);

    this.loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder);
    this.assets = {};
    this.mixers = [];
    this.effects = [];
    this.chunks = new Map();
    this.cars = [];
    this.avatars = [];
    this.roofY = 4.3;
    this.mySeat = -1;
    this.smooth = [{ lean: 0, duck: 0 }, { lean: 0, duck: 0 }];
    this.steamTimer = 0;
    this.orbit = 0;
    addEventListener('resize', () => this.resize());
    this.resize();
  }

  skyTexture() {
    const c = document.createElement('canvas');
    c.width = 4; c.height = 256;
    const g = c.getContext('2d');
    const grad = g.createLinearGradient(0, 0, 0, 256);
    grad.addColorStop(0, '#6f8fb8');
    grad.addColorStop(0.45, '#d7a77c');
    grad.addColorStop(0.62, '#e9b07a');
    grad.addColorStop(1, '#b56a3c');
    g.fillStyle = grad;
    g.fillRect(0, 0, 4, 256);
    const t = new THREE.CanvasTexture(c);
    t.colorSpace = THREE.SRGBColorSpace;
    return t;
  }

  resize() {
    const w = window.innerWidth, h = window.innerHeight;
    this.renderer.setSize(w, h, false);
    this.camera.aspect = w / h;
    // Keep about 64 degrees across on a wide screen; a phone held upright gets a closer view of the other cowboy.
    const hFov = (this.camera.aspect < 1 ? 50 : 64) * Math.PI / 180;
    this.camera.fov = Math.min(100, Math.max(58, 2 * Math.atan(Math.tan(hFov / 2) / this.camera.aspect) * 180 / Math.PI));
    this.camera.updateProjectionMatrix();
  }

  async load(onProgress) {
    const names = ['deputy', 'gunslinger', 'revolver', 'locomotive', 'boxcar', 'flatcar', 'caboose', ...CHUNKS, 'flash', 'tracer', 'steam', 'hat_deputy', 'hat_gunslinger'];
    let done = 0;
    await Promise.all(names.map(async (name) => {
      this.assets[name] = await this.loader.loadAsync(`/assets/${name}.glb`);
      onProgress(++done / names.length);
    }));
    this.buildTrain();
    this.buildAvatars();
    this.buildRevolver();
  }

  // ---------------------------------------------------------------- the train

  placeModel(gltf, shadows = true) {
    const root = gltf.scene;
    root.traverse((o) => {
      if (o.isMesh) { o.castShadow = shadows && !MOBILE; o.receiveShadow = !MOBILE; o.frustumCulled = !o.isSkinnedMesh; }
    });
    return root;
  }

  buildTrain() {
    // Modelled facing -Y in Blender, which is +Z here. A quarter turn puts them on the line, front towards +x.
    const order = ['locomotive', 'flatcar', 'boxcar', 'boxcar', 'caboose'];
    const players = 2;                                  // index of the boxcar the duellists stand on
    const parts = order.map((name, i) => {
      const src = this.assets[name];
      const scene = i === 3 ? cloneSkinned(src.scene) : src.scene;
      const car = new THREE.Group();
      car.add(scene);
      scene.rotation.y = Math.PI / 2;
      this.placeModel({ scene });
      const box = new THREE.Box3().setFromObject(car);
      const roll = src.animations.find((a) => a.name.endsWith('_roll'));
      let mixer = null;
      if (roll) { mixer = new THREE.AnimationMixer(scene); mixer.clipAction(roll).play(); this.mixers.push(mixer); }
      return { car, len: box.max.x - box.min.x, mid: (box.max.x + box.min.x) / 2, top: box.max.y, mixer };
    });
    // Couple them up, 0.7 m apart, the duellists' boxcar centred on the origin.
    const centre = [];
    centre[players] = 0;
    for (let i = players - 1; i >= 0; --i) { centre[i] = centre[i + 1] + parts[i + 1].len / 2 + 0.7 + parts[i].len / 2; }
    for (let i = players + 1; i < parts.length; ++i) { centre[i] = centre[i - 1] - parts[i - 1].len / 2 - 0.7 - parts[i].len / 2; }
    parts.forEach((p, i) => { p.car.position.set(centre[i] - p.mid, RAIL_Y, 0); this.scene.add(p.car); });
    this.cars = parts;
    this.roofY = RAIL_Y + parts[players].top - 0.05;
    this.stack = parts[0].car;
  }

  updateTrack(seed, distance) {
    const first = Math.floor((distance - BEHIND_M) / CHUNK_M);
    const last = Math.floor((distance + AHEAD_M) / CHUNK_M);
    for (const [i, chunk] of this.chunks) {
      if (i < first || i > last) { this.scene.remove(chunk); this.chunks.delete(i); }
    }
    for (let i = first; i <= last; ++i) {
      let chunk = this.chunks.get(i);
      if (!chunk) {
        chunk = this.assets[CHUNKS[pick(seed, i)]].scene.clone(true);
        chunk.traverse((o) => { if (o.isMesh) { o.receiveShadow = !MOBILE; } });
        this.chunks.set(i, chunk);
        this.scene.add(chunk);
      }
      chunk.position.x = i * CHUNK_M - distance;
    }
  }

  // ---------------------------------------------------------------- the cowboys

  buildAvatars() {
    for (const [seat, name] of [[0, 'deputy'], [1, 'gunslinger']]) {
      const gltf = this.assets[name];
      const body = gltf.scene;
      this.placeModel({ scene: body });
      const root = new THREE.Group();
      const lean = new THREE.Group();
      root.add(lean);
      lean.add(body);
      this.scene.add(root);
      const mixer = new THREE.AnimationMixer(body);
      this.mixers.push(mixer);
      const actions = {};
      for (const clip of gltf.animations) { actions[clip.name] = mixer.clipAction(clip); }
      for (const once of ['quickdraw', 'shoot', 'hit', 'death_back']) {
        if (actions[once]) { actions[once].setLoop(THREE.LoopOnce, 1); actions[once].clampWhenFinished = true; }
      }
      let hatBone = null, muzzle = null, head = null;
      body.traverse((o) => {
        if (o.isBone && o.name === 'hat') { hatBone = o; }
        if (o.name === 'muzzle_r') { muzzle = o; }
        if (o.isBone && o.name === 'head') { head = o; }
      });
      const avatar = { seat, root, lean, body, mixer, actions, current: null, busyUntil: 0, hatBone, muzzle, head, hat: name === 'deputy' ? 'hat_deputy' : 'hat_gunslinger' };
      const s = SEATS[seat];
      root.position.set(s.x, this.roofY, 0);
      root.rotation.y = s.facing > 0 ? Math.PI / 2 : -Math.PI / 2;
      this.play(avatar, 'idle');
      this.avatars.push(avatar);
    }
  }

  play(avatar, name, fade = 0.2) {
    const next = avatar.actions[name];
    if (!next || avatar.current === name) { return; }
    const prev = avatar.current && avatar.actions[avatar.current];
    next.reset().play();
    if (prev) { prev.crossFadeTo(next, fade, false); }
    avatar.current = name;
  }

  playOnce(seat, name, holdMs = 450) {
    const a = this.avatars[seat];
    if (!a) { return; }
    a.current = null;
    this.play(a, name, 0.08);
    a.busyUntil = performance.now() + holdMs;
  }

  // ---------------------------------------------------------------- the gun in your hand

  buildRevolver() {
    const gltf = this.assets.revolver;
    const gun = gltf.scene;
    this.placeModel({ scene: gun }, false);
    gun.traverse((o) => { if (o.isMesh) { o.renderOrder = 10; } });
    this.gun = new THREE.Group();
    this.gun.add(gun);
    this.camera.add(this.gun);
    this.gunMixer = new THREE.AnimationMixer(gun);
    this.mixers.push(this.gunMixer);
    this.gunActions = {};
    for (const clip of gltf.animations) { this.gunActions[clip.name] = this.gunMixer.clipAction(clip); }
    for (const once of ['fp_draw', 'fp_fire', 'fp_holster', 'fp_reload']) {
      const a = this.gunActions[once];
      if (a) { a.setLoop(THREE.LoopOnce, 1); a.clampWhenFinished = true; }
    }
    this.gunActions.fp_idle?.play();
    gun.traverse((o) => { if (o.name === 'muzzle') { this.gunMuzzle = o; } });
    this.gunDown = 1;
    this.recoil = 0;
  }

  gunAnim(name) {
    const a = this.gunActions[name];
    if (!a) { return; }
    for (const other of Object.values(this.gunActions)) { if (other !== a) { other.stop(); } }
    a.reset().play();
    if (name === 'fp_fire' || name === 'fp_draw') {
      const idle = this.gunActions.fp_idle;
      clearTimeout(this.gunIdleTimer);
      this.gunIdleTimer = setTimeout(() => { a.stop(); idle?.reset().play(); }, a.getClip().duration * 1000);
    }
  }

  // ---------------------------------------------------------------- shots and effects

  /** The ray under a screen point, in roof space (what the server judges). */
  rayAt(ndcX, ndcY) {
    const ray = new THREE.Raycaster();
    ray.setFromCamera({ x: ndcX, y: ndcY }, this.camera);
    const o = ray.ray.origin, d = ray.ray.direction;
    return { o: [o.x, o.y - this.roofY, o.z], d: [d.x, d.y, d.z] };
  }

  toWorld(p) { return new THREE.Vector3(p[0], p[1] + this.roofY, p[2]); }

  muzzleOf(seat) {
    if (seat === this.mySeat && this.gun) {
      const p = new THREE.Vector3();
      (this.gunMuzzle || this.gun).getWorldPosition(p);
      return p;
    }
    const a = this.avatars[seat];
    const p = new THREE.Vector3();
    if (a?.muzzle) { a.muzzle.getWorldPosition(p); return p; }
    return this.toWorld(pose(seat, 0, 0).shoulder);
  }

  shotFx(seat, end) {
    const from = this.muzzleOf(seat);
    const to = this.toWorld(end);
    const flash = this.assets.flash.scene.clone(true);
    flash.traverse((o) => { if (o.isMesh) { o.material = new THREE.MeshBasicMaterial({ color: '#ffd27a', transparent: true, opacity: 1, blending: THREE.AdditiveBlending, depthWrite: false }); } });
    flash.position.copy(from);
    flash.lookAt(to);
    flash.scale.setScalar(seat === this.mySeat ? 0.35 : 0.8);
    this.addEffect(flash, 0.07);
    const dir = to.clone().sub(from);
    const len = dir.length();
    const tracer = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, len, 5, 1, true), new THREE.MeshBasicMaterial({ color: '#ffe2a0', transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false }));
    tracer.position.copy(from).addScaledVector(dir, 0.5);
    tracer.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
    this.addEffect(tracer, 0.12, { fade: true });
    if (seat === this.mySeat) { this.recoil = 1; this.gunAnim('fp_fire'); }
    else { this.playOnce(seat, 'shoot', 380); }
  }

  hatOff(seat) {
    const a = this.avatars[seat];
    if (!a) { return; }
    const hat = this.assets[a.hat].scene.clone(true);
    const head = new THREE.Vector3();
    if (a.head) { a.head.getWorldPosition(head); } else { head.copy(this.toWorld(pose(seat).head)); }
    hat.position.copy(head).add(new THREE.Vector3(0, 0.15, 0));
    const away = SEATS[seat].facing * -1;
    this.addEffect(hat, 2.4, { velocity: new THREE.Vector3(away * 2.5 - 6, 4.5, (Math.random() - 0.5) * 2), gravity: 9.8, spin: new THREE.Vector3(6, 3, 4) });
    if (a.hatBone) { a.hatBone.scale.setScalar(0.0001); }
  }

  addEffect(obj, life, opts = {}) {
    this.scene.add(obj);
    this.effects.push({ obj, life, age: 0, ...opts });
  }

  tickEffects(dt) {
    for (const e of this.effects) {
      e.age += dt;
      if (e.velocity) {
        e.velocity.y -= (e.gravity || 0) * dt;
        e.obj.position.addScaledVector(e.velocity, dt);
        if (e.spin) { e.obj.rotation.x += e.spin.x * dt; e.obj.rotation.y += e.spin.y * dt; e.obj.rotation.z += e.spin.z * dt; }
      }
      if (e.rise) { e.obj.position.y += e.rise * dt; e.obj.position.x -= e.drift * dt; e.obj.scale.multiplyScalar(1 + dt * 0.8); }
      if (e.fade) {
        const k = 1 - e.age / e.life;
        e.obj.traverse((o) => { if (o.material) { o.material.opacity = Math.max(0, k) * (e.opacity ?? 0.9); } });
      }
    }
    this.effects = this.effects.filter((e) => {
      if (e.age < e.life) { return true; }
      this.scene.remove(e.obj);
      e.obj.traverse((o) => { if (o.geometry && e.ownGeometry !== false && o.geometry.dispose) { /* shared clones: leave */ } });
      return false;
    });
  }

  steam(dt, speed) {
    this.steamTimer -= dt;
    if (this.steamTimer > 0 || !this.stack) { return; }
    this.steamTimer = 0.14;
    const puff = this.assets.steam.scene.clone(true);
    puff.traverse((o) => { if (o.isMesh) { o.material = new THREE.MeshLambertMaterial({ color: '#efe6dc', transparent: true, opacity: 0.5, depthWrite: false }); } });
    const loco = this.stack.position;
    puff.position.set(loco.x + 3.3, this.roofY + 1.6, (Math.random() - 0.5) * 0.3);
    puff.scale.setScalar(0.5);
    this.addEffect(puff, 2.2, { rise: 2.2, drift: speed * 0.55, fade: true, opacity: 0.5 });
  }

  // ---------------------------------------------------------------- every frame

  /**
   * view: { serverNow, seed, rideStart, mySeat, players: [{ seat, input, hats, connected }], phase, step, me (my local
   * input: lean, duck, aim) }
   */
  update(dt, view) {
    const speed = RULES.trainSpeed;
    const distance = view.rideStart ? Math.max(0, (view.serverNow - view.rideStart) / 1000 * speed) : performance.now() / 1000 * speed;
    this.updateTrack(view.seed || 1, distance);
    for (const car of this.cars) { if (car.mixer) { car.mixer.timeScale = speed / 3.1 * 0.8; } }
    this.steam(dt, speed);

    // Cowboys: where the server says the other one is leaning and ducking, smoothed; you as your own controls say.
    for (const a of this.avatars) {
      // Outside a room both cowboys stand on the roof for the title screen.
      const p = view.players ? view.players.find((q) => q.seat === a.seat) : { seat: a.seat, input: {}, hats: RULES.hats };
      a.root.visible = Boolean(p) && (a.seat !== view.mySeat || view.phase === 'lobby');
      if (!p) { continue; }
      const input = a.seat === view.mySeat ? view.me : p.input;
      const sm = this.smooth[a.seat];
      const k = 1 - Math.exp(-dt * 14);
      sm.lean += ((input.lean || 0) - sm.lean) * k;
      sm.duck += ((input.duck || 0) - sm.duck) * k;
      const s = SEATS[a.seat];
      // Lean tips the body over from the feet, head 0.6 m out at full lean: the same as the hit model in shared.js.
      a.root.position.set(s.x, this.roofY, 0);
      a.lean.rotation.z = Math.asin(Math.min(1, LEAN_M / 1.7)) * sm.lean;
      if (a.hatBone) { a.hatBone.scale.setScalar(p.hats >= RULES.hats ? 1 : 0.0001); }
      if (performance.now() < a.busyUntil) { continue; }
      let want = 'idle';
      if (view.phase === 'results') { want = p.hats <= 0 ? 'death_back' : 'idle'; }
      else if (view.phase === 'match') {
        if (sm.duck > 0.5) { want = 'cover_idle'; }
        else if (view.step === 'draw') { want = 'aim'; }
        else { want = 'showdown_idle'; }
      }
      this.play(a, want);
    }

    // Camera: at your own eye, looking at the other cowboy. Or, outside a duel, a slow look at the train.
    if (view.mySeat >= 0 && view.phase !== 'lobby') {
      const sm = this.smooth[view.mySeat];
      const me = pose(view.mySeat, sm.lean, sm.duck);
      const them = SEATS[1 - view.mySeat];
      const t = performance.now() / 1000;
      this.camera.position.set(me.eye[0], me.eye[1] + this.roofY + Math.sin(t * 7.3) * 0.004, me.eye[2]);
      this.camera.lookAt(them.x, this.roofY + 1.25, me.eye[2] * 0.3);
      this.camera.rotateZ(Math.sin(t * 1.7) * 0.006 - sm.lean * 0.05);
      this.gun.visible = view.phase === 'match';
      const down = view.step === 'draw' ? 0 : 1;
      this.gunDown += (down - this.gunDown) * (1 - Math.exp(-dt * 10));
      this.recoil += (0 - this.recoil) * (1 - Math.exp(-dt * 12));
      const aim = view.me.aim || { x: 0, y: 0 };
      this.gun.position.set(0.16 + aim.x * 0.06, -0.19 - this.gunDown * 0.35 + aim.y * 0.04, -0.42 + this.recoil * 0.05);
      this.gun.rotation.set(aim.y * 0.25 + this.recoil * 0.25 - this.gunDown * 0.9, -aim.x * 0.3, 0);
    } else {
      this.orbit += dt * 0.05;
      const r = 17;
      this.camera.position.set(Math.cos(this.orbit) * r * 0.9, this.roofY + 3.5, Math.sin(this.orbit) * r + 2);
      this.camera.lookAt(0, this.roofY + 0.5, 0);
      if (this.gun) { this.gun.visible = false; }
    }

    for (const m of this.mixers) { m.update(dt); }
    this.tickEffects(dt);
    this.renderer.render(this.scene, this.camera);
  }
}
