// Runs Pele's layered layout (TypeScript, bundled with esbuild) on random graphs and writes
// the inputs and results as plain text for tests/test_layered.cpp.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  entryPoints: ['/home/claude/pele/src/layout/layered.ts'],
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/layered_bundle.mjs', out.outputFiles[0].text);
const L = await import('/tmp/layered_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const count = Number(process.argv[3] ?? 300);
const lines = [];
const f = (v) => (Object.is(v, -0) ? '0' : String(v));

for (let c = 0; c < count; c++) {
  const n = ri(1, c % 10 === 0 ? 40 : 12);
  const mode = c % 5; // 0 plain, 1 spans, 2 spans+labels, 3 tracks, 4 spans+pins/fixed
  const nodes = [];
  for (let i = 0; i < n; i++) {
    const nd = L.lnode(pick([40, 60, 80, 100, 120.5, 64.25]), pick([30, 40, 48, 60]));
    if (mode === 1 || mode === 2 || mode === 4) nd.span = pick([0, nd.w - 8, nd.w * 0.8]);
    if (mode === 4 && rnd() < 0.15) nd.pin = pick([-1, 1]);
    nodes.push(nd);
  }
  const m = n < 2 ? 0 : ri(0, Math.min(3 * n, 40));
  const edges = [];
  for (let i = 0; i < m; i++) {
    const a = ri(0, n - 1);
    let b = ri(0, n - 1);
    if (b === a && rnd() < 0.8) b = (a + 1) % n;
    if (a === b) continue;
    const e = L.ledge(a, b, rnd() < 0.8 ? 1 : ri(1, 3));
    if ((mode === 2 || mode === 4) && rnd() < 0.5) { e.labelW = pick([20, 33.5, 60]); e.labelH = 21; }
    if (mode === 4 && rnd() < 0.15) { e.tailFixed = true; e.tailDx = pick([-10, 0, 12]); }
    if (mode === 4 && rnd() < 0.15) { e.headFixed = true; e.headDx = pick([-10, 0, 12]); }
    edges.push(e);
  }
  const opt = {
    nodeSep: pick([40, 50, 30]), edgeSep: pick([16, 10, 20]), rankSep: pick([48, 60, 40]), portSep: pick([16, 20, 24]),
    tracks: mode === 3, straight: rnd() < 0.15, apart: rnd() < 0.2, headRoom: pick([0, 8, 12]),
  };
  lines.push(`case ${c}`);
  lines.push(`opts ${opt.nodeSep} ${opt.edgeSep} ${opt.rankSep} ${opt.portSep} ${+opt.tracks} ${+opt.straight} ${+opt.apart} ${opt.headRoom}`);
  lines.push(`nodes ${n}`);
  for (const nd of nodes) lines.push(`${f(nd.w)} ${f(nd.h)} ${nd.kind} ${nd.pin} ${f(nd.span)}`);
  lines.push(`edges ${edges.length}`);
  for (const e of edges) lines.push(`${e.tail} ${e.head} ${e.minlen} ${f(e.labelW)} ${f(e.labelH)} ${f(e.tailDx)} ${f(e.headDx)} ${+e.tailFixed} ${+e.headFixed}`);
  let res;
  try { res = L.layered(nodes, edges, opt); } catch (err) { lines.push('error'); continue; }
  lines.push(`result ${f(res.width)} ${f(res.height)}`);
  for (const nd of nodes) lines.push(`n ${f(nd.x)} ${f(nd.y)} ${nd.rank}`);
  for (const e of edges) lines.push(`e ${+e.reversed} ${f(e.labelX)} ${f(e.labelY)} ${f(e.tailDx)} ${f(e.headDx)} ${e.points.length} ${e.points.map(f).join(' ')}`);
  lines.push('end');
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/layered_cases.txt', lines.join('\n') + '\n');
console.log('cases', count, 'lines', lines.length);
