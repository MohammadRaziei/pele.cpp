// Runs pie's numberScanner (src/diagrams/pie/parser.ts) from every position of random text made of
// digits, dots and minus signs, for tests/test_scanner.cpp. One scanner serves one text, as the
// tokenizer uses it, because it remembers the digits it has read.
//   node tools/gen_scanner_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { PIE_TOKENS } from './diagrams/pie/parser.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/scanner_bundle.mjs', out.outputFiles[0].text);
const { PIE_TOKENS } = await import('/tmp/scanner_bundle.mjs');
const number = PIE_TOKENS.find((t) => t.name === 'NUMBER_PIE');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const PIECES = ['0', '0', '1', '5', '9', '00', '000', '0000', '12', '123', '.', '.', '..', '-', '-', ' ', 'a', ':', '\n', '1.5', '0.5', '-0', '12.', '.5', '9'.repeat(30), '0'.repeat(40)];
const lines = [];
const count = Number(process.argv[3] ?? 4000);
for (let c = 0; c < count; c++) {
  let s = '';
  for (let n = ri(1, c < 50 ? 3 : 12); n > 0; n--) s += pick(PIECES);
  // The tokenizer asks from positions in increasing order, but some positions are skipped (a
  // scanner is only asked where a token can start), so ask from a random increasing subset.
  const scan = number.scanner();
  const positions = [];
  for (let p = 0; p < s.length; p++) if (rnd() < 0.7) positions.push(p);
  const answers = positions.map((p) => scan(s, p));
  lines.push(`s ${hex16(s)} ${positions.length} ${positions.join(',') || '-'} ${answers.join(',') || '-'}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/scanner_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
