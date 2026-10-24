// Runs every regex literal of Pele on random input with JavaScript's RegExp, and writes what
// matched, for tests/test_regex.cpp. Needs /tmp/regexes.json from tools/extract_regexes.mjs.
import fs from 'fs';

const list = JSON.parse(fs.readFileSync(process.argv[2] ?? '/tmp/regexes.json', 'utf8'));
let seed = Number(process.argv[3] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const perRegex = Number(process.argv[4] ?? 40);
const BASE = [' ', ' ', '\t', '\n', '\r\n', 'a', 'b', 'x', 'A', 'Z', '0', '1', '9', '-', '_', '.', ',', ':', ';', '"', "'", '(', ')', '[', ']', '{', '}', '%', '#', '<', '>', '=', '/', '\\', '*', '+', '?', '|', '&', '!', '@', '$', '^', '~', 'é', '日', '\u00a0', '\u2028', 'title', 'accTitle', 'accDescr', '--', '-->', '%%', '%%{', '}%%', 'end', 'graph', 'true', 'false', '10', '3.5'];

const out = [];
const regexLines = [];
list.forEach((r, idx) => {
  regexLines.push(`${hex8(r.flags.replace(/[gy]/g, ''))} ${hex8(r.source)}`);
  const own = new Set(); for (const ch of r.source.replace(/\\./g, '')) if (!own.has(ch)) own.add(ch);
  const alphabet = [...BASE, ...own].filter((x) => x.length > 0);
  const base = r.flags.replace(/[gy]/g, '');
  const sticky = new RegExp(r.source, base + 'y');
  const global = new RegExp(r.source, base + 'g');
  for (let t = 0; t < perRegex; t++) {
    let s = '';
    for (let n = ri(0, t < 8 ? 6 : 30); n > 0; n--) s += pick(alphabet);
    const at = ri(0, s.length);
    const run = (re, p) => { re.lastIndex = p; const m = re.exec(s); return m === null ? '-' : `${m.index}:${m.index + m[0].length}:` + Array.from({ length: m.length }, (_, k) => (m[k] === undefined ? 'u' : k === 0 ? `${m.index}-${m.index + m[0].length}` : (() => { const ix = m.indices; return 'g' + k + '=' + hex16(m[k]); })())).join(','); };
    out.push(`m ${idx} y ${hex16(s)} ${at} ${run(sticky, at)}`);
    out.push(`m ${idx} g ${hex16(s)} ${at} ${run(global, at)}`);
  }
});
fs.writeFileSync('tests/data/regexes.txt', regexLines.join('\n') + '\n');
fs.writeFileSync(process.argv[5] ?? 'tests/data/regex_cases.txt', out.join('\n') + '\n');
console.log('regexes', list.length, 'cases', out.length);
