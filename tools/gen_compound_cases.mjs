// Runs Pele's compound layout (TypeScript, bundled with esbuild) on random nested graphs.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({ entryPoints: ['/home/claude/pele/src/layout/compound.ts'], bundle: true, format: 'esm', write: false, platform: 'node' });
fs.writeFileSync('/tmp/compound_bundle.mjs', out.outputFiles[0].text);
const C = await import('/tmp/compound_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const f = (v) => (Object.is(v, -0) ? '0' : String(v));
const DIRS = ['TB', 'BT', 'LR', 'RL'];

const count = Number(process.argv[3] ?? 300);
const lines = [];
for (let c = 0; c < count; c++) {
  const n = ri(1, c % 8 === 0 ? 30 : 12);
  const nodes = [];
  for (let i = 0; i < n; i++) {
    const nd = C.cnode(pick([40, 60, 80, 100, 120.5, 64.25]), pick([30, 40, 48, 60]));
    nd.isGroup = i > 0 && rnd() < 0.3;
    if (nd.isGroup) {
      nd.padX = pick([0, 12, 16]); nd.padTop = pick([0, 28, 32]); nd.padBottom = pick([0, 12]); nd.minW = pick([0, 0, 90]);
      if (rnd() < 0.5) nd.dir = pick(DIRS);
    }
    nd.seq = rnd() < 0.8 ? i : ri(0, 20);
    nd.span = rnd() < 0.5 ? pick([0, nd.w - 8, nd.w * 0.8]) : 0;
    nodes.push(nd);
  }
  const groups = nodes.map((nd, i) => (nd.isGroup ? i : -1)).filter((i) => i >= 0);
  for (let i = 0; i < n; i++) {
    if (groups.length && rnd() < 0.6) {
      // mostly parents with smaller index; sometimes any group (can make cycles)
      const cand = rnd() < 0.9 ? groups.filter((g) => g < i) : groups;
      if (cand.length) nodes[i].parent = pick(cand);
    }
  }
  const m = n < 2 ? 0 : ri(0, Math.min(3 * n, 30));
  const edges = [];
  for (let i = 0; i < m; i++) {
    const a = ri(0, n - 1);
    const b = rnd() < 0.05 ? a : ri(0, n - 1);
    const e = C.cnode ? { src: a, dst: b, minlen: rnd() < 0.8 ? 1 : ri(1, 3), labelW: 0, labelH: 0, route: [], labelX: 0, labelY: 0 } : null;
    if (rnd() < 0.4) { e.labelW = pick([20, 33.5, 60]); e.labelH = 21; }
    edges.push(e);
  }
  const opt = { nodeSep: pick([40, 50, 30]), edgeSep: pick([16, 10]), rankSep: pick([48, 60]), portSep: pick([16, 24]), tracks: false, straight: rnd() < 0.15, apart: rnd() < 0.2, headRoom: pick([0, 8]) };
  const rootDir = pick(DIRS);
  lines.push(`case ${c}`);
  lines.push(`opts ${opt.nodeSep} ${opt.edgeSep} ${opt.rankSep} ${opt.portSep} 0 ${+opt.straight} ${+opt.apart} ${opt.headRoom} ${rootDir}`);
  lines.push(`nodes ${n}`);
  for (const nd of nodes) lines.push(`${f(nd.w)} ${f(nd.h)} ${nd.parent} ${+nd.isGroup} ${nd.dir ?? '-'} ${f(nd.padX)} ${f(nd.padTop)} ${f(nd.padBottom)} ${f(nd.minW)} ${f(nd.seq)} ${f(nd.span)}`);
  lines.push(`edges ${edges.length}`);
  for (const e of edges) lines.push(`${e.src} ${e.dst} ${e.minlen} ${f(e.labelW)} ${f(e.labelH)}`);
  let res;
  try { res = C.compoundLayout(nodes, edges, rootDir, opt); } catch (err) { lines.push('error'); continue; }
  lines.push(`result ${f(res.width)} ${f(res.height)}`);
  for (const nd of nodes) lines.push(`n ${f(nd.x)} ${f(nd.y)} ${f(nd.w)} ${f(nd.h)}`);
  for (const e of edges) lines.push(`e ${f(e.labelX)} ${f(e.labelY)} ${e.route.length} ${e.route.map(f).join(' ')}`);
  lines.push('end');
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/compound_cases.txt', lines.join('\n') + '\n');
console.log('cases', count, 'errors', lines.filter((l) => l === 'error').length);
