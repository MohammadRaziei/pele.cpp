// Renders random cynefin diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_cynefin_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const DOMAINS = ['complex', 'complicated', 'clear', 'chaotic', 'confusion'];
const ITEMS = ['Emergent market shifts', 'Regulatory change', 'Customer feedback', 'A much longer item description that has to wrap around the badge width', 'x', '日本語', 'é ü ñ', 'Fix <b>bold</b> &amp; more', 'fa:fa-book Docs', 'Two words', "it's", '😀 emoji', 'Outage', 'Budget'];
const LABELS = ['escalate', 'de-escalate', 'a longer transition label that is wide', 'x', '日本語', 'Fix & go', ''];

function body() {
  const lines = [];
  const used = new Set();
  const blocks = ri(0, 7);
  for (let b = 0; b < blocks; b++) {
    const d = pick(DOMAINS);
    used.add(d);
    lines.push('  ' + d);
    const n = d === 'confusion' && rnd() < 0.5 ? ri(0, 7) : ri(0, 4);
    for (let i = 0; i < n; i++) lines.push('    "' + pick(ITEMS).replace(/"/g, '') + '"');
  }
  const trans = ri(0, 7);
  for (let t = 0; t < trans; t++) {
    const a = pick(DOMAINS), c = rnd() < 0.08 ? a : pick(DOMAINS);
    lines.push(`  ${a} --> ${c}` + (rnd() < 0.5 ? ` : "${pick(LABELS).replace(/"/g, '')}"` : ''));
    if (rnd() < 0.25) lines.push(`  ${a} --> ${c}` + (rnd() < 0.5 ? ` : "again"` : ''));
  }
  if (rnd() < 0.03) lines.push(pick(['  foo', '  complex -->', '  --> clear', '  complex : "x"', '  "orphan"', '  %% c', '']));
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_cynefin';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.1) src += `---\ntitle: ${pick(ITEMS).replace(/[:#<&]/g, '')}\n---\n`;
  if (rnd() < 0.45) {
    const cfg = {};
    if (rnd() < 0.5) cfg.seed = pick([1, 2, 42, -7, 123456789, 1e15, 2 ** 31, 2 ** 32 + 5, 0.5, 1.5e10, 0, -1e15, 1e16, 'x']);
    if (rnd() < 0.4) cfg.boundaryAmplitude = pick([0, 4, 8, 20, 50, 51, -1]);
    if (rnd() < 0.3) cfg.width = pick([100, 300, 640, 1000, 99, 20000]);
    if (rnd() < 0.3) cfg.height = pick([100, 300, 440, 800, 99]);
    if (rnd() < 0.3) cfg.showDomainDescriptions = pick([false, true, 'no']);
    src += `%%{init: {"cynefin": ${JSON.stringify(cfg)}}}%%\n`;
  }
  src += pick(['cynefin-beta', 'cynefin-beta', 'cynefin-beta:', 'cynefin-beta  ', '\n\ncynefin-beta', '%% c\ncynefin-beta']) + '\n';
  if (rnd() < 0.2) src += `  title ${pick(ITEMS).replace(/[<&]/g, '')}\n`;
  if (rnd() < 0.06) src += '  accTitle: Cynefin acc\n';
  if (rnd() < 0.06) src += '  accDescr: A cynefin description\n';
  for (const line of body()) src += line + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.015 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', ':', '-', '>', '\n', ' ', '%']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.3) options.idPrefix = pick(['a', 'mermaid-1', 'pele', 'é日本', 'x'.repeat(40), 'id-42']);
  if (rnd() < 0.1) options.fontSize = pick([10, 12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  if (Object.keys(options).length) { fs.writeFileSync(optPath, Object.entries(options).map(([k, v]) => `${k}=${v}`).join('\n') + '\n'); withOpts++; }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, options).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', withOpts);
