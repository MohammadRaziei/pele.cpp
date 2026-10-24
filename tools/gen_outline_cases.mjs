// Runs Pele's outline tokenizer (src/diagrams/common/outline.ts), in kanban and mindmap mode, on
// random text, and `s !== s.toLowerCase()` on random strings, for tests/test_outline.cpp.
//   node tools/gen_outline_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { tokenize } from './diagrams/common/outline.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/outline_bundle.mjs', out.outputFiles[0].text);
const { tokenize } = await import('/tmp/outline_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const PIECES = ['kanban', 'mindmap', 'Kanban', 'MINDMAP', 'kanbanx', 'mindmap_', ' ', ' ', '  ', '    ', '\t', '\n', '\n', '\r\n', '\n  ', '\n    ', '%%', '%% comment', '  %% c', '::icon(fa fa-book)', '::icon(', '::ICON(x)', ':::class', ':::', '::: a b',
  '[', ']', '(', ')', '((', '))', '{{', '}}', '-)', '(-', '{', '}', '"', '`', '"`', '`"', '@{', '@', 'id', 'node1', 'text with spaces', 'a<br/>b', 'x&amp;y', '日本語', 'é', '\u00a0', '\u2028', '\u3000', '😀', '\ud83d', '1', '-', ':', '^', 'label: x', "ticket: 'T-1'", ',', '\\'];
const lines = [];
const count = Number(process.argv[3] ?? 3000);
for (let c = 0; c < count; c++) {
  let s = rnd() < 0.6 ? pick(['kanban', 'mindmap', 'kanban\n', 'mindmap\n']) : '';
  for (let n = ri(0, c < 40 ? 4 : 22); n > 0; n--) s += pick(PIECES) + (rnd() < 0.3 ? pick(['', ' ', '\n']) : '');
  const kanban = rnd() < 0.5;
  const t = tokenize(s, kanban);
  lines.push(`t ${kanban ? 1 : 0} ${hex16(s)} ${t.types.length} ` + t.types.map((ty, i) => `${ty}:${hex16(t.texts[i])}:${t.starts[i]}`).join(','));
}
const CASED = ['A', 'z', 'É', 'ß', 'ΑΣ', 'Σ', 'İ', 'K', 'Ω', 'ǅ', 'Ⅻ', 'ⓐ', 'Ⓐ', '𐐀', '𝐀', '😀', '日本', 'ς', 'ı', '_', ' ', '0', 'ẞ', 'ᲀ', '\ud801', '\udc00', 'Ǆ', 'ǆ', 'Ꟃ'];
for (let c = 0; c < count; c++) {
  let s = '';
  for (let n = ri(0, 6); n > 0; n--) s += rnd() < 0.2 ? String.fromCodePoint(ri(0x20, 0x2fff)) : rnd() < 0.05 ? String.fromCodePoint(ri(0x10000, 0x1ffff)) : pick(CASED);
  lines.push(`l ${hex16(s)} ${s !== s.toLowerCase() ? 1 : 0}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/outline_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
