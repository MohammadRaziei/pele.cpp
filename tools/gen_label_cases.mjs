// Runs Pele's label layout (TypeScript, bundled with esbuild) on random label text.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { layoutLabel } from './text/label.ts'; export { metricsMeasurer } from './text/measurer.ts'; export { withIcons } from './text/icons.ts'; export { decodeEntities } from './text/entities.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/label_bundle.mjs', out.outputFiles[0].text);
const T = await import('/tmp/label_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const f = (v) => (Object.is(v, -0) ? '0' : String(v));

const TOKENS = [
  'hello', 'World', 'a', 'long_word_here', 'x_y_z', 'Çafé', '日本語', '漢字テスト', '한국어', 'wrap me please', ' ', ' ', ' ', '  ', '\t',
  '<br/>', '<br>', '<BR />', '\\n', '\n', '<b>', '</b>', '<i>', '</i>', '<strong>', '</strong>', '<em>', '</em>', '<span class="x">', '</span>', '<', '>', '<3',
  '*', '**', '_', '__', '*x*', '**y**', '_z_', '&amp;', '&lt;', '&#65;', '&#x41;', '&unknown;', '&#xZZ;', '&', '&#0;', '&#1114112;', '&#xD800;',
  'fa:fa-car', 'fab:fa-github', 'fas:fa-star', 'fa:fa-missing', 'faz:fa-x', ' fa:fa-car ', 'fa:fa-',
  '<sub>', '</sub>', '<sup>', '</sup>', '<code>', '</code>', '<u>', '</u>', '<kbd>', '</kbd>', '<samp>', '<tt>', '<var>', '<cite>', '<dfn>', '<ins>', '</ins>',
  '<SUB>', '</Sup>', '<div>', '</div>', '<mark>', '<s>', 'List<T>', '<foo>', '</foo>', '<a href="x">', '<p/>', '<b', '<sub x=1>',
  '\u00ad', '&shy;', 'ab\u00adcd', 'long\u00adword\u00adhere', '\u200b', 'path\u200bto\u200bfile', 'x\u00ad ', ' \u00ady', '\u00ad\u00ad', 'sup\u00ad)', '&AMP;', '&LT;', '&lsqb;', '&Hat;',
  '、', '。', '（', '）', '「', '」', '，', 'ー', '😀', 'e\u0301', '\u200b', 'ﬂ°°65¶ß', 'ﬂ°amp¶ß', 'ﬂ°nosuch¶ß', 'ﬂ°', '¶ß', '\r\n', '\r', '    indented', '\n    more',
];

const known = new Set(['fa:fa-car', 'fab:fa-github', 'fas:fa-star']);
const resolver = (n) => (known.has(n) ? '<svg/>' : null);

const count = Number(process.argv[3] ?? 500);
const lines = [];
for (let c = 0; c < count; c++) {
  const n = ri(0, c % 10 === 0 ? 30 : 10);
  let raw = '';
  for (let i = 0; i < n; i++) raw += pick(TOKENS);
  const markdown = rnd() < 0.4;
  const size = pick([12, 14, 16, 13.5]);
  const maxWidth = pick([40, 60, 120, 200, 1000]);
  const base = pick([0, 0, 1, 2, 4, 5]);
  const label = T.withIcons(resolver, () => T.layoutLabel(raw, markdown, T.metricsMeasurer, size, maxWidth, base));
  lines.push(`case ${c} ${+markdown} ${size} ${maxWidth} ${base} ${hex(raw)} ${hex(T.decodeEntities(raw))}`);
  lines.push(`label ${f(label.width)} ${f(label.height)} ${f(label.size)} ${f(label.lineHeight)} ${label.lines.length}`);
  label.lines.forEach((line, li) => {
    lines.push(`line ${f(label.widths[li])} ${line.length}`);
    for (const s of line) lines.push(`span ${s.style} ${s.icon ?? '-'} ${f(s.width)} ${hex(s.text)}`);
  });
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/label_cases.txt', lines.join('\n') + '\n');
console.log('cases', count);
