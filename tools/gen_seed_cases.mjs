// Runs cynefin's seededRandom and hashString (src/diagrams/cynefin/boundaries.ts) on random seeds and
// strings, for tests/test_seed.cpp. Numbers are written as the bits of the double.
//   node tools/gen_seed_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { seededRandom, hashString, wave, cliff, FOLD, HORIZON } from './diagrams/cynefin/boundaries.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/seed_bundle.mjs', out.outputFiles[0].text);
const { seededRandom, hashString, wave, FOLD, HORIZON } = await import('/tmp/seed_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const bits = (v) => { const b = Buffer.alloc(8); b.writeDoubleBE(v); return b.toString('hex'); };
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const lines = [];
const count = Number(process.argv[3] ?? 3000);
const SEEDS = [0, 1, -1, 2 ** 31, 2 ** 31 - 1, -(2 ** 31), 2 ** 32, 2 ** 32 + 5, 1e15, -1e15, 0.5, 1.5e10, 123456789, 0x6d2b79f5, -0x6d2b79f5, 4294967295, 4294967296, 1e10 + 0.7];
for (let c = 0; c < count; c++) {
  const k = ri(0, 3);
  const v = k === 0 ? pick(SEEDS) + ri(-5, 5) : k === 1 ? ri(-(2 ** 31), 2 ** 31) : k === 2 ? (rnd() - 0.5) * 2e15 : ri(-100, 100) + rnd();
  lines.push(`r ${bits(v)} ${bits(seededRandom(v))}`);
}
const CH = ['a', 'b', 'Z', '0', '-', '_', 'é', '日', '😀', '\ud800', 'pele', 'mermaid-', 'x'.repeat(30)];
for (let c = 0; c < count; c++) {
  let s = '';
  for (let n = ri(0, 8); n > 0; n--) s += pick(CH);
  lines.push(`h ${hex16(s)} ${hashString(s)}`);
}
for (let c = 0; c < count / 3; c++) {
  const vertical = rnd() < 0.5, o = vertical ? FOLD : HORIZON;
  const opt = { ...o, segments: ri(1, 8), pinStart: rnd() < 0.3, pinEnd: rnd() < 0.5 };
  const args = [ri(0, 200) + rnd(), ri(201, 800) + rnd(), ri(0, 400) + rnd(), ri(-1000, 1000) + (rnd() < 0.3 ? rnd() : 0), ri(0, 50) + (rnd() < 0.3 ? rnd() : 0)];
  const curve = wave(...args, opt);
  lines.push(`w ${args.map(bits).join(',')} ${opt.segments} ${vertical ? 1 : 0} ${opt.pinStart ? 1 : 0} ${opt.pinEnd ? 1 : 0} ${curve.map(bits).join(',')}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/seed_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
