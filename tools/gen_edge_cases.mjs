// Runs Pele's svg/edges.ts (routePath, marker, edgeLabelSvg, loopPath, sideReach) on random input.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export * from './svg/edges.ts'; export { layoutLabel } from './text/label.ts'; export { metricsMeasurer } from './text/measurer.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/edge_bundle.mjs', out.outputFiles[0].text);
const E = await import('/tmp/edge_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const f = (v) => (Object.is(v, -0) ? '0' : String(v));
const CURVES = [undefined, 'linear', 'step', 'stepBefore', 'stepAfter', 'basis', 'cardinal'];
const MARKERS = ['arrow_point', 'arrow_circle', 'arrow_cross', 'none', 'arrow_open'];

const lines = [];
const count = Number(process.argv[3] ?? 600);
for (let c = 0; c < count; c++) {
  const k = c % 5;
  if (k <= 2) {
    // a route: mostly vertical/horizontal legs with S bends, like the layout makes
    const n = ri(2, 7);
    const route = [];
    let x = ri(0, 300), y = ri(0, 300);
    const axis0 = pick([0, 1]);
    for (let i = 0; i < n; i++) {
      route.push(x, y, rnd() < 0.8 ? axis0 : 1 - axis0);
      const dir = rnd();
      if (dir < 0.35) y += ri(-120, 120);
      else if (dir < 0.7) x += ri(-120, 120);
      else { x += ri(-80, 80); y += ri(-80, 80); }
      if (rnd() < 0.1) { x = route[route.length - 3]; y = route[route.length - 2]; }
      if (rnd() < 0.15) { x += pick([0.005, 0.5, 0.004]); }
    }
    const curve = pick(CURVES);
    const st = pick([0, 7, 8, 4]), et = pick([0, 7, 8, 4]);
    const useReach = rnd() < 0.5;
    const sr = useReach ? pick([0, 7, 14, 12, 6]) : undefined, er = useReach ? pick([0, 7, 14, 12, 6]) : undefined;
    const p = E.routePath(route, curve, st, et, sr, er);
    lines.push(`route ${route.length} ${route.map(f).join(' ')} ${curve ?? '-'} ${st} ${et} ${f(sr)} ${f(er)} ${hex8(p.d)} ${[p.sx, p.sy, p.sdx, p.sdy, p.ex, p.ey, p.edx, p.edy].map(f).join(' ')}`);
  } else if (k === 3) {
    const t = pick(MARKERS);
    const x = ri(-50, 300) + pick([0, 0.5, 0.125]), y = ri(-50, 300), a = pick([[0, 1], [1, 0], [0, -1], [-1, 0], [0.6, 0.8], [-0.28, 0.96]]);
    const col = pick(['var(--_l)', '#fff']);
    lines.push(`marker ${t} ${x} ${y} ${a[0]} ${a[1]} ${hex8(col)} ${hex8(E.marker(t, x, y, a[0], a[1], col))} ${E.markerTrim(t)}`);
  } else {
    const cx = ri(0, 300), cy = ri(0, 300), w = pick([40, 80, 120.5]), h = pick([30, 40, 60]), kk = ri(0, 4), sideways = rnd() < 0.5, base = pick([20, 28]), trim = pick([0, 7, 8]);
    const e = { labelW: pick([0, 33.5]), labelH: pick([0, 21]), labelX: 0, labelY: 0 };
    const p = E.loopPath({ x: cx, y: cy }, w, h, kk, sideways, base, trim, e);
    lines.push(`loop ${cx} ${cy} ${w} ${h} ${kk} ${+sideways} ${base} ${trim} ${f(e.labelW)} ${f(e.labelH)} ${hex8(p.d)} ${[p.sx, p.sy, p.sdx, p.sdy, p.ex, p.ey, p.edx, p.edy, e.labelX, e.labelY].map(f).join(' ')}`);
    const raw = pick(['Yes', 'a long label here', 'x<br/>y', '<b>bold</b> text']);
    const label = E.layoutLabel(raw, false, E.metricsMeasurer, 14, 200, 0);
    const id = pick(['e1', 'a&amp;b', 'x"y']);
    const lx = ri(0, 200) + 0.5, ly = ri(0, 200), at = pick(['', ' class="pele-edge-text"']);
    lines.push(`elabel ${hex8(id)} ${hex16(raw)} ${lx} ${ly} ${hex8(at)} ${hex8(E.edgeLabelSvg(id, label, lx, ly, at, undefined))}`);
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/edge_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
