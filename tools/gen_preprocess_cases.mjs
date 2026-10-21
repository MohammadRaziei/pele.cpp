// Runs Pele's preprocess, cleanupComments and encodeEntities on random sources.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({ entryPoints: ['/home/claude/pele/src/preprocess.ts'], bundle: true, format: 'esm', write: false, platform: 'node' });
fs.writeFileSync('/tmp/pre_bundle.mjs', out.outputFiles[0].text);
const P = await import('/tmp/pre_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
function ser(v) {
  if (v === null) return 'N';
  if (v === true) return 'T';
  if (v === false) return 'F';
  if (typeof v === 'number') return 'n' + (Object.is(v, -0) ? '0' : String(v));
  if (typeof v === 'string') return 's' + hex16(v);
  if (Array.isArray(v)) return '[' + v.map(ser).join(',') + ']';
  return '{' + Object.keys(v).map((k) => hex16(k) + '=' + ser(v[k])).join(',') + '}';
}

const FRONT = [
  '---\ntitle: My Title\n---\n', '---\ntitle: T\nconfig:\n  theme: dark\n  flowchart:\n    curve: basis\n---\n', '---\nconfig:\n  gantt:\n    barHeight: 5\ndisplayMode: compact\n---\n',
  '  ---\n  title: Indented\n  ---\n', '---\n---\n', '---\ntitle: [unclosed\n---\n', '---\ntitle: "a: b"\n---\r\n', '---\ntitle: One\n---\nA-->B\n---\nx: y\n---\n', '--- \ntitle: Sp\n--- \n',
  '---\ndisplayMode: 7\nconfig:\n  gantt:\n    x: 1\n---\n', '---\n- a\n- b\n---\n', '---\ntitle: 0\n---\n', '---\nconfig: 5\n---\n', '--- not front\n', '---\ntitle: nofence\n',
];
const DIRECT = [
  '%%{init: {"theme":"dark"}}%%', "%%{init: {'theme':'forest', 'flowchart': {'curve':'linear'}}}%%", '%%{wrap}%%', '%%{ init : { "a": { "b": 1 } } }%%', '%%{init: not json}%%', '%%{initialize: {"x":1}}%%', '%%{INIT: {"x":1}}%%',
  '%%{init: theme}%%', '%%{ }%%', '%%{', '%%{init: {"a":1}', '%%{init:\n{"a":\n2}\n}%%', '%%{init: {"__proto__": {"x":1}, "constructor": 1}}%%', '%%{wrap: true}%%', '%%{init: {"a":[1,2,{"b":3}]}}%%',
];
const LINES = ['graph TD', 'flowchart LR', 'A-->B', '  B --> C', '%% a comment', '%% another', '  %% indented comment', '%%', '%%{not a comment', '', '   ', 'x %% not at start', 'style A fill:#f9f,stroke:#333;', 'classDef c fill:#ff0;', 'A["a #35; b"]', 'B["#quot;q#quot;"]', 'C[#x41;]', 'D[#a-b;]', 'E["#;"]', '<b class="x" id="y">bold</b>', '<a href="u">l</a> <br/>', 'N["<i title=\'t\'>z</i>"]', '<div a="1"', 'a < b > c', '\ttabbed', '\u2028sep'];

const lines = [];
const count = Number(process.argv[3] ?? 600);
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.45) src += pick(FRONT);
  if (rnd() < 0.5) src += pick(DIRECT) + pick(['\n', '', ' ', '\n\n']);
  for (let i = ri(0, 7); i > 0; i--) { src += pick(LINES) + pick(['\n', '\n', '\r\n', '\r']); if (rnd() < 0.1) src += pick(DIRECT) + '\n'; }
  if (rnd() < 0.2) src = src.replace(/\n$/, '');
  let res;
  try {
    const r = P.preprocess(src);
    res = `ok ${hex16(r.text)} ${r.title === undefined ? '-' : hex16(r.title)} ${ser(r.config)} ${hex16(r.withComments)} ${r.lineOffset}`;
  } catch (e) { res = 'err ' + hex16(String(e.message)); }
  lines.push(`pre ${hex16(src)} ${res}`);
  lines.push(`clean ${hex16(src)} ${hex16(P.cleanupComments(src))}`);
  lines.push(`enc ${hex16(src)} ${hex16(P.encodeEntities(src))}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/preprocess_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
