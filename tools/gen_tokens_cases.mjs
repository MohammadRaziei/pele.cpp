// Runs Pele's tokenizer (src/diagrams/common/tokens.ts) on random text with the token tables of
// every diagram whose tables are plain data (no custom `scanner` or `match`), and writes the
// tables and what came out, for tests/test_tokens.cpp.
//   node tools/gen_tokens_cases.mjs [seed] [casesPerTable] [tables.txt] [cases.txt]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const SOURCES = {
  cynefin: 'cynefin/parser.ts', wardley: 'wardley/parser.ts', eventmodeling: 'eventmodeling/parser.ts', peg: 'railroad/peg.ts', ir: 'railroad/ir.ts',
  ebnf: 'railroad/ebnf.ts', abnf: 'railroad/abnf.ts', pie: 'pie/parser.ts', git: 'git/parser.ts', treeview: 'treeview/parser.ts', packet: 'packet/parser.ts',
  radar: 'radar/parser.ts', architecture: 'architecture/parser.ts', treemap: 'treemap/parser.ts', info: 'info/parser.ts',
};
const NAMES = (k) => (k === 'peg' || k === 'ir' || k === 'ebnf' || k === 'abnf' ? k.toUpperCase() : k === 'eventmodeling' ? 'EVENTMODELING' : k.toUpperCase()) + '_TOKENS';
const entry = Object.entries(SOURCES).map(([k, f]) => `export { ${NAMES(k)} as ${k} } from './diagrams/${f}';`).join('\n') +
  `\nexport { tokenize, titleValue, accTitleValue, accDescrValue, stringValue } from './diagrams/common/tokens.ts';`;
const out = await build({ stdin: { contents: entry, resolveDir: '/home/claude/pele/src', loader: 'ts' }, bundle: true, format: 'esm', write: false, platform: 'node' });
fs.writeFileSync('/tmp/tokens_bundle.mjs', out.outputFiles[0].text);
const M = await import('/tmp/tokens_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const BASE = [' ', ' ', '\t', '\n', '\r\n', 'a', 'b', 'x', 'A', 'Z', '0', '1', '9', '-', '_', '.', ',', ':', ';', '"', "'", '(', ')', '[', ']', '{', '}', '%', '%%', '#', '<', '>', '=', '/', '\\', '*', '+', '?', '|', '&', '!', '@', '$', '^', '~', '`', 'é', '日', '\u00a0', '\u2028', '😀', '---', '%%{', '}%%', 'title', 'accTitle', 'accDescr', 'accTitle: t', 'accDescr: d', 'accDescr {\n d\n}', 'title T', 'true', 'false', '10', '3.5', '-1'];

const tableLines = [];
const caseLines = [];
const perTable = Number(process.argv[3] ?? 300);
let id = 0;
const used = [];
for (const key of Object.keys(SOURCES)) {
  const types = M[key];
  if (types.some((t) => t.scanner || t.match)) { console.log('skip (custom scanners):', key); continue; }
  const t = id++;
  used.push(key);
  tableLines.push(`table ${t} ${types.length} ${key}`);
  const pieces = [...BASE];
  for (const ty of types) {
    const lines = [ty.name, typeof ty.pattern === 'string' ? 'L' : 'R', hex8(typeof ty.pattern === 'string' ? ty.pattern : ty.pattern.source), ty.hidden ? 1 : 0, ty.opener ? hex8(ty.opener.source) : '-', ty.first !== undefined ? hex8(ty.first) : '-', ty.longer ? ty.longer.map((a) => types.indexOf(a)).join(',') || '-' : '-'];
    lines[0] = hex8(ty.name);
    tableLines.push('t ' + lines.join(' '));
    if (typeof ty.pattern === 'string') pieces.push(ty.pattern);
    else { for (const m of ty.pattern.source.matchAll(/[A-Za-z][A-Za-z0-9_-]{1,12}/g)) pieces.push(m[0]); }
  }
  for (const w of types.filter((x) => typeof x.pattern !== 'string').map((x) => x.name)) if (/^[A-Za-z]/.test(w)) pieces.push(w);
  // Half the cases use only what the table's own patterns are made of, so that most of them tokenize.
  const own = pieces.slice(BASE.length).concat([' ', '\n', '\t', '\r\n', 'a', 'b', 'x', '10', 'title', 'accTitle: t', '%% c', '---\ntitle: t\n---\n', '%%{init: {}}%%\n']);
  for (let c = 0; c < perTable; c++) {
    let s = '';
    const from = c % 2 === 0 ? own : pieces;
    for (let n = ri(0, c < 20 ? 4 : 25); n > 0; n--) s += pick(from) + (rnd() < 0.5 ? pick(['', ' ', ' ', '\n']) : '');
    let result;
    try {
      const tk = M.tokenize(s, types, key);
      result = 'ok ' + tk.kinds.map((k, i) => `${k}:${tk.starts[i]}:${tk.ends[i]}`).join(',');
    } catch (e) { result = 'err ' + hex8(e.message); }
    caseLines.push(`c ${t} ${hex16(s)} ${result}`);
  }
}
// The value functions, on text shaped like the tokens that feed them.
const VALUE_PIECES = ['title', 'accTitle', 'accDescr', ':', ' ', '  ', '\t', '\n', '\r', '\n\n', '{', '}', 'text', 'a b', 'x', '%%', '"', "'", '\\', 'n', 't', '0', 'é', '\u00a0', '日本'];
const mk = () => { let s = ''; for (let n = ri(0, 14); n > 0; n--) s += pick(VALUE_PIECES); return s; };
for (let c = 0; c < perTable * 3; c++) {
  const kind = pick(['title', 'accTitle', 'accDescr', 'string']);
  let s = mk();
  if (kind === 'title') s = pick(['title', 'title ', 'title\t', ' title x', 'title  a   b ']) + s;
  if (kind === 'accTitle') s = pick(['accTitle:', 'accTitle :', 'accTitle: ']) + s;
  if (kind === 'accDescr') s = pick(['accDescr:', 'accDescr {', 'accDescr{', 'accDescr :', 'accDescr {\n', 'accDescr  {\n  ']) + s + pick(['', '}', '\n}', ' }']);
  if (kind === 'string') s = pick(['"', "'"]) + s.replace(/["']/g, '') + pick(['"', "'"]);
  let result;
  try { result = 'ok ' + hex16({ title: M.titleValue, accTitle: M.accTitleValue, accDescr: M.accDescrValue, string: M.stringValue }[kind](s)); } catch (e) { result = 'throw'; }
  caseLines.push(`v ${kind} ${hex16(s)} ${result}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/token_tables.txt', tableLines.join('\n') + '\n');
fs.writeFileSync(process.argv[5] ?? 'tests/data/token_cases.txt', caseLines.join('\n') + '\n');
console.log('tables', used.join(','), 'cases', caseLines.length);
