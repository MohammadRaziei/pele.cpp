// Runs Pele's svg/shapes.ts on every shape name, and on route insetting.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export * from './svg/shapes.ts'; export { cnode } from './layout/compound.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/shape_bundle.mjs', out.outputFiles[0].text);
const S = await import('/tmp/shape_bundle.mjs');
const src = fs.readFileSync('/home/claude/pele/src/svg/shapes.ts', 'utf8');
const names = [...src.matchAll(/^  (?:'([\w-]+)'|(\w+)): \{$/gm)].map((m) => m[1] ?? m[2]).filter((n) => n !== 'size');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const f = (v) => (Object.is(v, -0) ? '0' : String(v));

const lines = [];
const all = [...new Set(names), 'unknown', 'constructor', '__proto__', 'toString'];
for (const name of all) {
  for (let r = 0; r < 12; r++) {
    const tw = pick([0, 20, 47.5, 80, 133.25, 200]), th = pick([0, 17, 21, 42, 63]);
    const sz = S.shapeSize(name, tw, th);
    const a = pick([' fill="var(--_s)" stroke="var(--_b)"', ' fill="var(--_s)" stroke="var(--_b)" style="fill:red"']), line = pick([' stroke="var(--_b)"', ' stroke="var(--_l)" stroke-width="2"']);
    const drawn = S.drawShape(name, sz.w, sz.h, a, line);
    const insets = [], spans = [];
    for (let side = 0; side < 4; side++) for (const t of [0, 7.5, -12, 40]) insets.push(f(S.shapeInset(name, sz.w, sz.h, side, t)));
    for (const sw of [false, true]) spans.push(f(S.shapeSpan(name, sz.w, sz.h, sw)));
    lines.push(`shape ${name} ${tw} ${th} ${f(sz.w)} ${f(sz.h)} ${f(sz.dy)} ${hex8(a)} ${hex8(line)} ${hex8(drawn)} ${+S.shapeHasLabel(name)} ${insets.join(',')} ${spans.join(',')}`);
  }
}
for (let c = 0; c < 400; c++) {
  const name = pick(all);
  const node = S.cnode(pick([100, 120.5]), pick([60, 48]));
  node.x = ri(0, 200); node.y = ri(0, 200); node.isGroup = rnd() < 0.1;
  const view = { shape: name, w: node.w - pick([0, 10]), h: node.h - pick([0, 6]) };
  const kind = ri(0, 3);
  const route = [0, 0, 0];
  if (kind === 0) { route[0] = node.x + pick([0, 10, -15]); route[1] = node.y - node.h / 2; }
  else if (kind === 1) { route[0] = node.x + pick([0, 10, -15]); route[1] = node.y + node.h / 2; }
  else if (kind === 2) { route[0] = node.x - node.w / 2; route[1] = node.y + pick([0, 8, -9]); }
  else { route[0] = node.x + node.w / 2 + pick([0, 0.4]); route[1] = node.y + pick([0, 8, -9]); }
  const before = route.slice();
  const side = S.endSide(route, 0, node);
  S.insetRoute(route, 0, view, node);
  lines.push(`route ${name} ${node.w} ${node.h} ${node.x} ${node.y} ${+node.isGroup} ${view.w} ${view.h} ${before.map(f).join(' ')} ${side} ${route.map(f).join(' ')}`);
}
fs.writeFileSync(process.argv[3] ?? 'tests/data/shape_cases.txt', lines.join('\n') + '\n');
console.log('shapes', all.length, 'lines', lines.length);
