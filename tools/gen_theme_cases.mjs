// Runs Pele's svg/theme.ts (fontStyle, resolveStyle, classNames, seriesColor) on random input.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({ entryPoints: ['/home/claude/pele/src/svg/theme.ts'], bundle: true, format: 'esm', write: false, platform: 'node' });
fs.writeFileSync('/tmp/theme_bundle.mjs', out.outputFiles[0].text);
const T = await import('/tmp/theme_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const f = (v) => (v === undefined ? '-' : String(v));

const FAMILIES = [undefined, '', 'Arial', 'Helvetica Neue, sans-serif', '"Fira Code", monospace', "'Open Sans', Arial", 'Inter,system-ui', 'Segoe UI Emoji', '日本語フォント, serif', 'a;b', 'url(x)', '"unterminated', 'Arial,', ',Arial', '  Arial  ,  Times New Roman  ', '"a"b', "'x'\n", 'Ünïcode Fönt', '123font', '-x', '_y', 'font.name', 'a b c', 'a\tb', '"with space and :colon"', '"ok", bad{', '😀 font', 'ΑΒΓ, ﬁne'];
const DECL = ['fill:#fff', 'fill: #f9f ', 'stroke:#333', 'stroke-width:4px', 'stroke-dasharray: 5 5', 'color:red', 'color: #fff !important', 'font-weight:bold', 'font-weight:600', 'font-weight:500', 'font-weight:bolder', 'font-style:italic', 'font-size:20px', 'font-size:14', 'font-size:0', 'font-size:300px', 'font-size:1.5em', 'rx:5', 'ry: 5', 'opacity:.5', 'FILL:#abc', 'Stroke : blue', 'background:url(x)', 'fill:url(#g)', 'fill:URL ( #g )', 'color:expression(1)', 'fill:javascript:1', 'fill:a;b', 'fill:{x}', 'fill:<x>', 'fill:a\\b', '@import x', 'fill:image-set(x)', 'fill:/*c*/red', ':nope', 'nocolon', 'fill:', 'text-decoration:underline', 'letter-spacing:2px', 'fill:#111;', 'font-family:Arial', 'unknown:1'];
const RAWCLASS = ['a', 'a b', ' a  b ', 'foo-bar baz_qux', 'a,b;c', '', '   ', 'x"y', '日本 a', 'a--b', '-', '_'];

const lines = [];
const count = Number(process.argv[3] ?? 500);
for (let c = 0; c < count; c++) {
  const k = c % 4;
  if (k === 0) {
    const fam = pick(FAMILIES), mono = pick(FAMILIES), size = pick([12, 14, 16, 13.5, 11.25]);
    lines.push(`font ${size} ${fam === undefined ? '-' : hex16(fam)} ${mono === undefined ? '-' : hex16(mono)} ${hex8(T.fontStyle(size, fam, mono))}`);
  } else if (k === 1 || k === 2) {
    const n = ri(0, 6);
    const decls = [];
    for (let i = 0; i < n; i++) decls.push(pick(DECL));
    const r = T.resolveStyle(decls);
    lines.push(`style ${n} ${decls.map(hex16).join(' ')} ${hex8(r.shape)} ${hex8(r.line)} ${hex8(r.text)} ${r.stroke === undefined ? '-' : hex8(r.stroke)} ${+r.bold} ${+r.italic} ${f(r.fontSize)}`);
  } else {
    if (c % 8 === 3) { const i = ri(-20, 20); lines.push(`series ${i} ${hex8(T.seriesColor(i))}`); }
    else { const raw = pick(RAWCLASS); lines.push(`class ${hex16(raw)} ${hex8(T.classNames(raw))}`); }
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/theme_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
