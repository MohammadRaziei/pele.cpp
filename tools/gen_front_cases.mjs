// Runs Pele's yaml, JSON.parse, detect and error helpers on random input.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { parseYaml } from './util/yaml.ts'; export * from './detect.ts'; export * from './errors.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/front_bundle.mjs', out.outputFiles[0].text);
const F = await import('/tmp/front_bundle.mjs');

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

const SCALARS = ['1', '-2', '+3', '0', '007', '1.5', '.5', '5.', '1e3', '1E-2', '0x1F', '0o17', '0b101', '-0x10', '.inf', '-.Inf', '.NaN', 'true', 'False', 'NULL', '~', 'null', 'hello', 'hello world', '"quoted"', "'single'", '"a\\nb"', '"\\u0041\\x42"', '"\\q"', "'it''s'", '"unterminated', '[1, 2, 3]', '{a: 1, b: two}', '[a, [b, c], {d: e}]', '{x: [1,2], y: {z: 1}}', '[1,,2]', '{a: 1 b: 2}', 'a # comment', '"a" # c', '#notvalue', 'yes', '2020-01-01', 'a: b', 'http://x.y', 'x:y', '- not list', '| ', 'multi word value', '[unclosed', '{k: v', '"tab\\tx"', '"\\U0001F600"', '"\\ud800"', '"\\x4"', '"line1\\\n   continued"'];
const KEYS = ['title', 'config', 'theme', 'a', 'b', 'c', 'key with space', '"quoted key"', "'sq'", '1', '2', 'x.y', 'flowchart', 'displayMode', '__proto__', 'constructor', '- dash'];

function genBlock(depth, indent) {
  const pad = ' '.repeat(indent);
  const lines = [];
  const kind = depth > 3 ? 0 : ri(0, 3);
  const n = ri(1, 4);
  for (let i = 0; i < n; i++) {
    if (kind === 2) {
      const r = ri(0, 3);
      if (r === 0) lines.push(`${pad}- ${pick(SCALARS)}`);
      else if (r === 1) { lines.push(`${pad}-`); lines.push(...genBlock(depth + 1, indent + 2)); }
      else if (r === 2) lines.push(`${pad}- ${pick(KEYS)}: ${pick(SCALARS)}`);
      else lines.push(`${pad}- ${pick(KEYS)}: ${pick(SCALARS)}`, `${pad}  ${pick(KEYS)}: ${pick(SCALARS)}`);
    } else {
      const key = pick(KEYS);
      const r = ri(0, 7);
      if (r <= 2 || depth > 3) lines.push(`${pad}${key}: ${pick(SCALARS)}`);
      else if (r === 3) { lines.push(`${pad}${key}:`); lines.push(...genBlock(depth + 1, indent + 2)); }
      else if (r === 4) lines.push(`${pad}${key}: |`, `${pad}  line one`, `${pad}    indented`, `${pad}  line three`);
      else if (r === 5) lines.push(`${pad}${key}: >-`, `${pad}  folded one`, `${pad}  folded two`, '', `${pad}  para`);
      else if (r === 6) lines.push(`${pad}${key}: plain`, `${pad}  continued plain`, `${pad}# comment`);
      else lines.push(`${pad}${key}:`, `${pad}- item1`, `${pad}- item2`);
    }
  }
  return lines;
}

const lines = [];
const count = Number(process.argv[3] ?? 600);
for (let c = 0; c < count; c++) {
  const k = c % 6;
  if (k <= 2) {
    let doc = genBlock(0, 0).join('\n');
    if (rnd() < 0.25 && doc.length > 3) { const at = ri(0, doc.length - 1); doc = rnd() < 0.5 ? doc.slice(0, at) + doc.slice(at + 1) : doc.slice(0, at) + pick([' ', ':', '"', '\t', '-', '#', '\n  ']) + doc.slice(at); }
    if (rnd() < 0.1) doc = pick(['{a: 1, b: [1, 2]}', '[1, 2, {x: y}]', '{', '[', '"x', '---\na: 1', 'a: 1\n...', 'a: 1\n---', '   ', '']);
    let res;
    try { res = 'ok ' + ser(F.parseYaml(doc)); } catch (e) { res = 'err ' + hex16(String(e.message)); }
    lines.push(`yaml ${hex16(doc)} ${res}`);
  } else if (k === 3) {
    const JSONS = ['{}', '[]', '{"a":1}', '{"a":{"b":[1,2,3],"c":null},"d":true}', '{"theme":"dark","flowchart":{"curve":"basis"}}', '[1,2', '{"a":}', "{'a':1}", '{"a":1,}', '{"a":01}', '{"a":1e5,"b":-0.5,"c":1E+2}', '"str\\u0041\\n"', '"bad\\x"', '{"__proto__":{"x":1}}', '  {"a" : [ ] }  ', '{"a":1,"a":2}', '{"2":1,"1":2,"b":3,"a":4}', 'tru', 'null', '-', '1.', '"\\ud83d\\ude00"', '{"a":"line\nbreak"}'];
    const doc = pick(JSONS);
    let res;
    try { res = 'ok ' + ser(JSON.parse(doc)); } catch { res = 'err'; }
    lines.push(`json ${hex16(doc)} ${res}`);
  } else if (k === 4) {
    const words = ['graph TD', 'flowchart LR', 'flowchart-elk TD', 'sequenceDiagram', 'classDiagram', 'stateDiagram-v2', 'erDiagram', 'pie', 'pie title X', 'gantt', 'gitGraph', 'mindmap', 'timeline', 'journey', 'xychart-beta', 'xychart', 'quadrantChart', 'requirementDiagram', 'requirement', 'C4Context', 'C4Container', 'sankey-beta', 'sankey', 'block-beta', 'block', 'packet-beta', 'kanban', 'architecture-beta', 'radar-beta', 'treemap', 'treemap-beta', 'info', 'agentflow-beta', 'agentflow-betax', 'usecase-beta', 'usecase-beta x', 'usecase-betax', 'railroad-beta', 'railroad-ebnf-beta', 'RAILROAD-PEG-BETA', 'treeView-beta', 'venn-beta', 'cynefin-beta', 'cynefin-beta:', 'cynefin-betax', 'wardley-beta', 'WARDLEY-BETA', 'swimlane-beta', 'swimlane-betax', 'ishikawa', 'ishikawa-beta', 'ISHIKAWA x', 'ishikawax', 'eventmodeling', 'hello', '', 'grap', 'x\nC4Component', 'text C4Dynamic here', 'a C4Deployment', ' C4Context', 'A graph'];
    const ws = pick(['', ' ', '\n', '  \t\n ', '\u00a0', '\u3000']);
    const doc = ws + pick(words) + pick(['', '\nA-->B', ' x']);
    const d = F.detectDiagram(doc);
    lines.push(`detect ${hex16(doc)} ${d ? d.type + ' ' + d.mermaidType : '- -'}`);
  } else {
    let src = '';
    for (let i = ri(0, 5); i > 0; i--) src += pick(['graph TD', 'A-->B', '', '  x  ', 'line']) + '\n';
    const offset = ri(0, src.length);
    const loc = F.locate(src, offset);
    const detail = pick(['Expecting X got Y', '']);
    const e1 = F.syntaxError('flowchart', src, offset, detail, false), e2 = F.syntaxError('pie', src, offset, detail, true);
    lines.push(`locate ${hex16(src)} ${offset} ${loc.line} ${loc.column} ${hex16(loc.snippet)} ${hex16(detail)} ${hex16(e1.message)} ${hex16(e2.message)} ${e1.code} ${e1.type}`);
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/front_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
