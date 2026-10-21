// Runs Pele's svg/builder.ts helpers (esc, escText, num, labelSvg) on random input.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export * from './svg/builder.ts'; export { layoutLabel } from './text/label.ts'; export { metricsMeasurer } from './text/measurer.ts'; export { withIcons } from './text/icons.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/svg_bundle.mjs', out.outputFiles[0].text);
const S = await import('/tmp/svg_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');

const TOKENS = [
  'hello', 'World', ' ', ' ', '  ', '<br/>', '\\n', '<b>', '</b>', '<i>', '</i>', '*', '**', '_', '&amp;', '&lt;', '&#65;', '&unknown;', '&', '<', '>', '"', "'",
  'fa:fa-car', 'fab:fa-github', 'fa:fa-missing', '日本語', '😀', '\u0001', '\u000b', '\u007f', 'a\tb', 'wrap me please for a while', 'x_y_z',
];
const known = new Set(['fa:fa-car', 'fab:fa-github']);
const resolver = (n) => (known.has(n) ? '<path d="M0 0L1 1"/>' : null);

const lines = [];
const count = Number(process.argv[3] ?? 500);
for (let c = 0; c < count; c++) {
  const kind = c % 4;
  if (kind === 0) {
    const pool = [() => ri(-2000, 2000) / 100, () => ri(-100000, 100000) / 1000, () => ri(0, 5000), () => rnd() * 1e4, () => -rnd() * 1e3, () => ri(0, 1000) + 0.005, () => ri(0, 1000) + 0.995, () => ri(1, 99) / 1000, () => rnd() * 1e12, () => -rnd() * 1e12, () => 0, () => -0, () => 1e-9, () => 0.004999, () => 0.005, () => -0.005, () => -0.004999];
    const v = pick(pool)();
    lines.push(`num ${Object.is(v, -0) ? '-0' : String(v)} ${S.num(v)}`);
  } else {
    let raw = '';
    const n = ri(0, 8);
    for (let i = 0; i < n; i++) raw += pick(TOKENS);
    if (kind === 1) {
      lines.push(`esc ${hex16(raw)} ${hex8(S.esc(raw))}`);
      lines.push(`esct ${hex16(raw)} ${hex8(S.escText(raw))}`);
    } else {
      const markdown = rnd() < 0.4, size = pick([12, 14, 16, 13.5]), maxWidth = pick([60, 120, 1000]), base = pick([0, 0, 1, 2, 4]);
      const cx = pick([0, 50, 100.5, -20.25]), cy = pick([0, 30, 77.75]);
      const attrs = pick(['', ' class="pele-title"', ' font-weight="700"', ' font-style="oblique"', ' class="a" font-weight="var(--_w)"']);
      const svg = S.withIcons(resolver, () => S.labelSvg(S.layoutLabel(raw, markdown, S.metricsMeasurer, size, maxWidth, base), cx, cy, attrs, resolver));
      lines.push(`lab ${+markdown} ${size} ${maxWidth} ${base} ${cx} ${cy} ${hex8(attrs)} ${hex16(raw)} ${hex8(svg)}`);
    }
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/svg_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
