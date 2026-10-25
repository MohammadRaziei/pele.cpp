// Renders random pie charts with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_pie_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const LABELS = ['Dogs', 'Cats', 'Rats', 'Birds', 'Fish', 'A much longer slice label that goes on', '日本語', 'é ü ñ', 'Fix <b>bold</b> &amp; more', 'x', 'Two words', "it's", '😀 emoji', 'a%b', 'Other', 'Apples', 'Oranges'];
const SAFE = ['1', '2', '5', '10', '42', '100', '0', '0.5', '1.5', '33.33', '66.67', '0.01', '99.99', '1000', '7', '3', '12.5', '25', '50', '75', '0.1', '0.2', '0.3', '-0', '1.00'];
const NUMBERS = ['1', '2', '5', '10', '42', '100', '0', '0.5', '1.5', '33.33', '66.67', '0.01', '99.99', '1000', '7', '3', '12.5', '25', '50', '75', '0.1', '0.2', '0.3', '-0', '000', '1.2.3', '12.', '-1', '-0.5', '1e3', '123456789012345678901234', '0.0001', '1.00', '9007199254740993'];
const TITLES = ['Pets', 'Favourite things', 'A long title for the pie chart that wraps maybe', '日本語', 'T'];

function body() {
  const lines = [];
  const n = rnd() < 0.08 ? 0 : ri(1, 9);
  const biased = rnd() < 0.25;
  for (let i = 0; i < n; i++) {
    const label = pick(LABELS);
    const num = biased && i % 3 === 0 ? pick(['1', '0.5', '0.01', '0.2']) : biased ? pick(['500', '1000', '300']) : rnd() < 0.9 ? pick(SAFE) : pick(NUMBERS);
    const q = rnd() < 0.85 ? '"' : "'";
    lines.push(`    ${q}${label.replace(new RegExp(q, 'g'), '')}${q} : ${num}`);
  }
  if (rnd() < 0.1) lines.push('    "Dogs" : 99');
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_pie';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(TITLES)}\n---\n`;
  if (rnd() < 0.45) {
    const cfg = {};
    if (rnd() < 0.5) cfg.textPosition = pick([0, 0.2, 0.5, 0.75, 0.9, 1, 1.5, -1, 'x']);
    if (rnd() < 0.5) cfg.donutHole = pick([0, 0.2, 0.5, 0.9, 0.95, 1, -1, 'x']);
    if (rnd() < 0.6) cfg.legendPosition = pick(['left', 'right', 'top', 'bottom', 'center', 'other', 5, true]);
    src += `%%{init: {"pie": ${JSON.stringify(cfg)}}}%%\n`;
  }
  src += (rnd() < 0.97 ? pick(['pie', 'pie', 'pie showData', 'pie  showData', '\n\npie']) : 'pie\n  showData') + '\n';
  if (rnd() < 0.3) src += `  title ${pick(TITLES)}\n`;
  if (rnd() < 0.1) src += '  accTitle: Pie acc\n';
  if (rnd() < 0.1) src += '  accDescr: A pie description\n';
  for (const line of body()) src += line + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.03 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', ':', '-', '.', '0', ' ', '\n']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.5) options.maxWidth = pick([200, 250, 320, 400, 500, 640, 800, 120, 60]);
  if (rnd() < 0.1) options.fontSize = pick([12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  if (Object.keys(options).length) { fs.writeFileSync(optPath, Object.entries(options).map(([k, v]) => `${k}=${v}`).join('\n') + '\n'); withOpts++; }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, options).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', withOpts);
