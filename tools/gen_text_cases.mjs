// Runs literalLabel and fitText (src/diagrams/common) with the default measurer on random text,
// for tests/test_text.cpp.   node tools/gen_text_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { literalLabel } from './diagrams/common/literal-label.ts'; export { fitText } from './diagrams/common/fit-text.ts'; export { defaultMeasurer } from './text/measurer.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/text_bundle.mjs', out.outputFiles[0].text);
const M = await import('/tmp/text_bundle.mjs');
const measurer = M.defaultMeasurer();

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const PIECES = ['a', 'b', 'W', 'm', 'i', 'l', '0', '9', ' ', ' ', ' ', '  ', '\n', '\t', '-', '_', '.', ',', 'é', 'ü', '日', '本', '語', '😀', '👍🏽', '\ud83d', '\ude00', '\u00a0', '\u2028', '\u3000', '…', 'Hello', 'world', 'packet', 'Source Port', 'Destination', 'Sequence Number', 'ReallyLongUnbrokenWordWithoutAnySpaces', 'x'.repeat(40), '<b>', '&amp;'];
const lines = [];
const count = Number(process.argv[3] ?? 1500);
for (let c = 0; c < count; c++) {
  let text = '';
  for (let n = ri(0, c < 30 ? 3 : 14); n > 0; n--) text += pick(PIECES);
  const size = pick([8, 10, 12, 14, 16, 18, 24, 12.5, 7]);
  const style = pick([0, 0, 0, 1, 2, 3, 4, 5]);
  const room = pick([1, 5, 12, 20, 33, 50, 80, 120, 200, 300, ri(1, 400), ri(1, 60) + 0.5]);
  const l = M.literalLabel(text, measurer, size, room, style);
  lines.push(`l ${hex16(text)} ${size} ${room} ${style} ${l.lines.length} ${l.width} ${l.height} ${l.lineHeight}`);
  for (let i = 0; i < l.lines.length; i++) lines.push(`${l.widths[i]} ${hex16(l.lines[i].map((s) => s.text).join(''))}`);
  const min = pick([4, 6, 8, 10]);
  const max = pick([-1, 0, 3, 10, 25, 40, 80, 160, ri(1, 300) + 0.25]);
  const f = M.fitText(text, max, size, min, measurer, style);
  lines.push(`f ${hex16(text)} ${max} ${size} ${min} ${style} ` + (f ? `${f.size} ${f.width} ${hex16(f.text)}` : '-'));
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/text_cases.txt', lines.join('\n') + '\n');
console.log('cases', count, 'lines', lines.length);
