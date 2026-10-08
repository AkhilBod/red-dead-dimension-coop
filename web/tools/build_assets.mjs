// Builds web/public/assets from the game's own exports (art/export), small enough for a phone:
// only the animations the web game plays, deduplicated, quantized and meshopt-compressed.
//   node tools/build_assets.mjs        (Blender FBX conversions go to tools/tmp first: tools/convert_fbx.py)
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { dedup, prune, quantize, meshopt, resample, weld } from '@gltf-transform/functions';
import { MeshoptEncoder } from 'meshoptimizer';
import fs from 'node:fs';
import path from 'node:path';

const ART = '../art/export';
const OUT = 'public/assets';
const COWBOY = ['idle', 'showdown_idle', 'quickdraw', 'aim', 'shoot', 'hit', 'death_back', 'cover_idle'];

const jobs = [
  [`${ART}/characters/SK_Deputy.glb`, 'deputy.glb', COWBOY],
  [`${ART}/characters/SK_Gunslinger.glb`, 'gunslinger.glb', COWBOY],
  [`${ART}/fx/SK_PlayerRevolver.glb`, 'revolver.glb', ['fp_idle', 'fp_draw', 'fp_fire', 'fp_holster', 'fp_reload']],
  [`${ART}/train/SK_Train_Locomotive.glb`, 'locomotive.glb', ['Train_Locomotive_roll']],
  [`${ART}/train/SK_Train_Boxcar.glb`, 'boxcar.glb', ['Train_Boxcar_roll']],
  [`${ART}/train/SK_Train_Flatcar_Crates.glb`, 'flatcar.glb', ['Train_Flatcar_Crates_roll']],
  [`${ART}/train/SK_Train_Caboose.glb`, 'caboose.glb', ['Train_Caboose_roll']],
  ['tools/tmp/SM_FG_Chunk_Flat_A.glb', 'chunk_flat_a.glb', []],
  ['tools/tmp/SM_FG_Chunk_Flat_B.glb', 'chunk_flat_b.glb', []],
  ['tools/tmp/Joined_Chunck_Cactus_A.glb', 'chunk_cactus.glb', []],
  ['tools/tmp/SM_FG_Chunk_Rocky_A.glb', 'chunk_rocky_a.glb', []],
  ['tools/tmp/SM_FG_Chunk_Rocky_B.glb', 'chunk_rocky_b.glb', []],
  ['tools/tmp/SM_FG_Chunk_Mesa_A.glb', 'chunk_mesa.glb', []],
  ['tools/tmp/SM_FG_Chunk_Telegraph_Fence_A.glb', 'chunk_fence.glb', []],
  ['tools/tmp/SM_MuzzleFlash_A.glb', 'flash.glb', []],
  ['tools/tmp/SM_Tracer_Player.glb', 'tracer.glb', []],
  ['tools/tmp/SM_Puff_Steam.glb', 'steam.glb', []],
  ['tools/tmp/SM_Hat_Deputy.glb', 'hat_deputy.glb', []],
  ['tools/tmp/SM_Hat_Gunslinger.glb', 'hat_gunslinger.glb', []],
];

await MeshoptEncoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.encoder': MeshoptEncoder });
fs.mkdirSync(OUT, { recursive: true });
let total = 0;
for (const [src, name, keep] of jobs) {
  const doc = await io.read(src);
  for (const anim of doc.getRoot().listAnimations()) {
    const clip = anim.getName().replace(/^.*\|/, '');
    if (!keep.includes(clip)) { anim.dispose(); } else { anim.setName(clip); }
  }
  await doc.transform(prune(), dedup(), weld(), resample(), quantize(), meshopt({ encoder: MeshoptEncoder, level: 'medium' }));
  const out = path.join(OUT, name);
  await io.write(out, doc);
  const size = fs.statSync(out).size;
  total += size;
  console.log(`${name.padEnd(22)} ${(size / 1024).toFixed(0).padStart(6)} KB  ${doc.getRoot().listAnimations().map((a) => a.getName()).join(' ')}`);
}
console.log(`total ${(total / 1048576).toFixed(1)} MB`);
