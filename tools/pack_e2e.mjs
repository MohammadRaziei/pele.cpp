// Packs a directory made by one of the gen_*_cases.mjs tools (N.mmd, N.svg or N.err, and N.opts)
// into one manifest for tests/test_e2e.cpp, keeping the SHA-256 of each expected SVG instead of
// the SVG itself. One line per case, tab-separated:
//   name, options (k=v;k=v or -), source, result (svg:<sha256> or err:<code>\n<message>)
// with \\, \n, \r and \t escaped in the source and the message.
//   node tools/pack_e2e.mjs <dir> <manifest.txt>
import crypto from 'crypto';
import fs from 'fs';
import path from 'path';

const [dir, out] = process.argv.slice(2);
if (!dir || !out) { console.error('usage: pack_e2e.mjs <dir> <manifest.txt>'); process.exit(2); }
const esc = (s) => s.replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t');
const names = fs.readdirSync(dir).filter((f) => f.endsWith('.mmd')).map((f) => f.slice(0, -4)).sort((a, b) => a.localeCompare(b, 'en', { numeric: true }));
const lines = [];
for (const n of names) {
  const p = (ext) => path.join(dir, n + ext);
  const opts = fs.existsSync(p('.opts')) ? fs.readFileSync(p('.opts'), 'utf8').trim().split('\n').join(';') : '-';
  const src = fs.readFileSync(p('.mmd'), 'utf8');
  const result = fs.existsSync(p('.svg')) ? 'svg:' + crypto.createHash('sha256').update(fs.readFileSync(p('.svg'))).digest('hex') : 'err:' + esc(fs.readFileSync(p('.err'), 'utf8'));
  lines.push([n, opts, esc(src), result].join('\t'));
}
fs.writeFileSync(out, lines.join('\n') + '\n');
console.log(`${names.length} cases, ${fs.statSync(out).size} bytes`);
