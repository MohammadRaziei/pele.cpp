// Runs the class diagram lexer (src/diagrams/class/lexer.ts) on random text, for tests/test_class_lexer.cpp.
//   node tools/gen_class_lexer_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { tokenize } from './diagrams/class/lexer.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/classlex_bundle.mjs', out.outputFiles[0].text);
const { tokenize } = await import('/tmp/classlex_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const P = ['classDiagram', 'classDiagram-v2', 'class', 'classDef', 'cssClass', 'callback', 'call', 'click', 'link', 'href', 'note', 'note for', 'namespace', 'style', 'direction TB', 'direction LR', 'direction RL', 'direction BT', 'direction XX', ' direction  TB  x', 'accTitle: t', 'accDescr: d', 'accDescr {\n multi\n}', 'accTitle', 'accDescr',
  '<<interface>>', '<<', '>>', '<|--', '--|>', '*--', '--*', 'o--', '--o', '<--', '-->', '..>', '<..', '..|>', '<|..', '--', '..', '()--', '--()', '-', '.', ':', '::', ':::', ': label', '"', '"quoted"', '`bq`', '`bq~G~`', '~', '~int~', 'List~int~', '[', ']', '[*]', '{', '}', '{ }', '(', ')', '()', '+', '#', '%', '%%', '%% comment', '=', ',', ';', '!', '?', '&', "'", '\\', '/', '$', '*',
  'Animal', 'Dog', 'Duck', 'x', '_', '_self', '_blank', '_parent', '_top', 'id1', ' ', ' ', '  ', '\t', '\n', '\n', '\r\n', '\n  ', 'é', '日本', '😀', '\u00a0', '\u2028', 'int age', '+swim()', '-name', 'void run()', '*abstract*', '$static$', 'cardinality "1"', '"1" --> "*"'];
const lines = [];
const count = Number(process.argv[3] ?? 4000);
for (let c = 0; c < count; c++) {
  let s = rnd() < 0.6 ? pick(['classDiagram\n', 'classDiagram-v2\n', 'classDiagram\n  ']) : '';
  for (let n = ri(0, c < 40 ? 6 : 26); n > 0; n--) s += pick(P) + (rnd() < 0.4 ? pick(['', ' ', '\n', '\n  ']) : '');
  const t = tokenize(s);
  lines.push(`t ${hex16(s)} ${t.types.length} ` + t.types.map((ty, i) => `${ty}:${hex16(t.texts[i])}:${t.starts[i]}`).join(','));
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/class_lexer_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
