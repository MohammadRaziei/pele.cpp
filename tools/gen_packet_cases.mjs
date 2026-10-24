// Renders random packet diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_packet_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const LABELS = ['Source Port', 'Destination Port', 'Sequence Number', 'Flags', 'Length', 'Checksum', 'Version', 'IHL', 'TTL', 'Protocol', 'Data', 'A', 'URG', '日本語', 'x &amp; y', 'a<br/>b', 'Reserved', 'ReallyLongUnbrokenFieldNameThatNeedsBreaking', 'Options (if any) and padding', 'é', '', 'Two Words', 'with \\"quote\\"', "it's", '😀 emoji'];
const TITLES = ['TCP Packet', 'UDP', 'A longer title for this packet diagram', '日本語', 'T'];

function body() {
  const lines = [];
  let bit = 0;
  const n = ri(1, 9);
  const quote = (s) => (rnd() < 0.85 ? `"${s}"` : `'${s.replace(/'/g, '')}'`);
  for (let i = 0; i < n; i++) {
    const width = pick([1, 1, 2, 4, 8, 8, 16, 16, 32, 48, ri(1, 70)]);
    const label = quote(pick(LABELS));
    const k = ri(0, 5);
    if (k === 0 && width > 1) lines.push(`${bit}-${bit + width - 1}: ${label}`);
    else if (k === 1) lines.push(`+${width}: ${label}`);
    else if (k === 2 && width === 1) lines.push(`${bit}: ${label}`);
    else if (k === 3) lines.push(`${bit}-${bit + width - 1}: ${label}`);
    else lines.push(`+${width}: ${label}`);
    bit += width;
  }
  if (rnd() < 0.08) lines.push(pick([`${bit + 3}: "gap"`, `${bit}-${bit - 1 > 0 ? bit - 2 : 0}: "backwards"`, '+0: "zero"', `${bit}-${bit + 100000}: "huge"`, `+5000: "wide"`, `${bit}-99999999999999999999999: "very huge"`]));
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_packet';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, narrow = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(TITLES)}\n---\n`;
  if (rnd() < 0.3) {
    const cfg = {};
    if (rnd() < 0.5) cfg.bitsPerRow = pick([1, 4, 8, 16, 24, 32, 64, 5000, 0, -3, 'x']);
    if (rnd() < 0.3) cfg.rowHeight = pick([10, 24, 40, 500, 0]);
    if (rnd() < 0.3) cfg.bitWidth = pick([10, 20, 32, 50, 500]);
    if (rnd() < 0.2) cfg.paddingX = pick([0, 2, 10, 40]);
    if (rnd() < 0.2) cfg.paddingY = pick([0, 2, 10, 40]);
    if (rnd() < 0.2) cfg.showBits = false;
    if (rnd() < 0.25) cfg.bitOrder = pick(['descending', 'ascending', 'other']);
    if (rnd() < 0.1) cfg.useMaxWidth = false;
    src += `%%{init: {"packet": ${JSON.stringify(cfg)}}}%%\n`;
  }
  src += pick(['packet-beta', 'packet', 'packet-beta', 'packet\n', 'packet-beta ']) + '\n';
  if (rnd() < 0.2) src += `  title ${pick(TITLES)}\n`;
  if (rnd() < 0.1) src += '  accTitle: Packet acc\n';
  if (rnd() < 0.1) src += '  accDescr: A packet description\n';
  for (const line of body()) src += pick(['  ', '', '\t']) + line + (rnd() < 0.03 ? ';\n' : pick(['\n', '\n', '\n', '\r\n']));
  if (rnd() < 0.08 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', ':', '-', '+', '0', ' ', '\n', ';']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.35) options.maxWidth = pick([200, 320, 400, 500, 639, 640, 800, 120, 60]);
  if (rnd() < 0.1) options.fontSize = pick([12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  if (Object.keys(options).length) { fs.writeFileSync(optPath, Object.entries(options).map(([k, v]) => `${k}=${v}`).join('\n') + '\n'); narrow++; }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, options).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', narrow);
