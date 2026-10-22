// Runs Pele's flowchart parser and db on random sources and dumps the model as text.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { parseFlowchart } from './diagrams/flowchart/parser.ts'; export { FlowDb } from './diagrams/flowchart/db.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/parser_bundle.mjs', out.outputFiles[0].text);
const P = await import('/tmp/parser_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const h = (v) => (v === undefined ? '-' : hex16(String(v)));
const list = (a) => (a === undefined ? '-' : `${a.length}${a.map((x) => ' ' + hex16(String(x))).join('')}`);
function ser(v) {
  if (v === undefined) return '-';
  if (v === null) return 'N';
  if (v === true) return 'T';
  if (v === false) return 'F';
  if (typeof v === 'number') return 'n' + (Object.is(v, -0) ? '0' : String(v));
  if (typeof v === 'string') return 's' + hex16(v);
  if (Array.isArray(v)) return '[' + v.map(ser).join(',') + ']';
  return '{' + Object.keys(v).map((k) => hex16(k) + '=' + ser(v[k])).join(',') + '}';
}
const num = (v) => (v === undefined ? '-' : Object.is(v, -0) ? '0' : String(v));

function dump(db) {
  const lines = [];
  lines.push(`dir ${h(db.direction)}`);
  for (const n of db.nodes.values()) {
    const props = n.props === undefined ? '-' : Object.keys(n.props).map((k) => hex16(k) + '=' + hex16(n.props[k])).join(',') || '=';
    lines.push(`node ${hex16(n.id)} ${h(n.text)} ${n.labelType} ${h(n.type)} ${list(n.styles)} ${list(n.classes)} ${h(n.dir)} ${props} ${h(n.link)} ${h(n.linkTarget)} ${h(n.icon)} ${h(n.form)} ${h(n.pos)} ${h(n.img)} ${h(n.constraint)} ${num(n.assetWidth)} ${num(n.assetHeight)}`);
  }
  for (const e of db.edges) lines.push(`edge ${hex16(e.start)} ${hex16(e.end)} ${h(e.type)} ${hex16(e.text)} ${e.labelType} ${h(e.stroke)} ${num(e.length)} ${list(e.style)} ${list(e.classes)} ${h(e.id)} ${+e.isUserDefinedId} ${h(e.interpolate)} ${ser(e.animate)} ${ser(e.animation)}`);
  for (const s of db.subgraphs) lines.push(`sub ${hex16(s.id)} ${hex16(s.title)} ${s.labelType} ${h(s.dir)} ${list(s.nodes)} ${list(s.classes)} ${ser(s.metadata)}`);
  const cls = [...db.classes.values()].map((c) => `class ${hex16(c.id)} ${list(c.styles)} ${list(c.textStyles)}`).sort();
  const tips = [...db.tooltips].map(([k, v]) => `tip ${hex16(k)} ${hex16(v)}`).sort();
  lines.push(...cls, ...tips);
  lines.push(`misc ${list(db.defaultEdgeStyle)} ${h(db.defaultInterpolate)} ${h(db.accTitle)} ${h(db.accDescr)}`);
  return lines;
}

const IDS = ['A', 'B', 'C', 'D', 'E', 'node1', 'start', 'é', '日本', 'n_1', 'a-b', 'end1'];
const TEXTS = ['text', 'two words', '"quoted"', '"quoted words"', '"`md`"', 'a<br/>b', '"with (paren)"', 'x&amp;y', '日本語', 'text', 'two words'];
const SHAPES = [(t) => `[${t}]`, (t) => `[${t}]`, (t) => `(${t})`, (t) => `(${t})`, (t) => `((${t}))`, (t) => `(((${t})))`, (t) => `{${t}}`, (t) => `{{${t}}}`, (t) => `[[${t}]]`, (t) => `[(${t})]`, (t) => `([${t}])`, (t) => `>${t}]`, (t) => `[/${t}/]`, (t) => `[\\${t}\\]`, (t) => `[/${t}\\]`, (t) => `[\\${t}/]`, (t) => `(-${t}-)`, (t) => `[|f:v|${t}]`];
const LINKS = ['-->', '---', '-.->', '-.-', '==>', '===', '~~~', '--x', '--o', 'x--x', 'o--o', '<-->', '--->', '---->', '-..->', '==>>'];
const META = ['A@{ shape: rect }', 'A@{ shape: cyl, label: "db" }', 'A@{ shape: Rect }', 'A@{ shape: nope }', 'A@{ icon: x, label: "" }', 'A@{ img: "u", w: 20, h: 30 }', 'A@{ label: "x", labelType: string }', 'e1@{ animate: true }', 'e1@{ curve: basis }', 'A@{\n  shape: doc\n  label: "multi"\n}', 'A@{ shape: [bad }', 'A@{ pos: t, form: square, constraint: on }'];

const node = () => pick(IDS) + (rnd() < 0.6 ? pick(SHAPES)(pick(TEXTS)) : '') + (rnd() < 0.1 ? ':::cls' : '');
const nodes = () => (rnd() < 0.2 ? `${node()} & ${node()}` : node());
function statement(depth) {
  const k = ri(0, 14);
  if (k <= 5) return `${nodes()} ${pick(LINKS)} ${nodes()}`;
  if (k === 5) return `${nodes()} ${pick(LINKS)}${rnd() < 0.5 ? '|' + pick(TEXTS) + '|' : ''} ${nodes()} ${pick(LINKS)} ${nodes()}`;
  if (k === 6) return `${nodes()} -- ${pick(TEXTS)} --> ${nodes()}`;
  if (k === 7) return pick(['style A fill:#f9f,stroke:#333', 'classDef c1 fill:#ff0,color:#000', 'classDef c1,c2 stroke:red;', 'class A,B c1', 'linkStyle default stroke:green', 'linkStyle default interpolate linear stroke:red', 'style Z fill:red', 'style A fill:#f00,stroke:#00f']);
  if (k === 8) return pick(['click A callback', 'click A call cb()', 'click A call cb(1,2) "tip"', 'click A href "http://x.y"', 'click A href "http://x.y" _blank', 'click A "http://x.y" "tip" _self', 'click A "javascript:alert(1)"', 'click A "u" "tip"', 'click A cb "tip"']);
  if (k === 9) return pick(['accTitle: My title', 'accDescr: My description', 'accDescr {\n  multi\n    line\n}']);
  if (k === 10) return `${pick(IDS)} ${pick(['e1@', 'e2@'])}${pick(LINKS)} ${pick(IDS)}`;
  if (k === 11) return pick(META);
  if (k === 12 && depth < 3) {
    const inner = [];
    for (let i = ri(0, 3); i > 0; i--) inner.push(statement(depth + 1));
    if (rnd() < 0.4) inner.push(pick(['direction TB', 'direction LR', 'direction RL', 'direction BT', 'direction TD']));
    const head = pick(['subgraph s' + ri(1, 3), 'subgraph s' + ri(1, 3) + '[Title ' + ri(1, 9) + ']', 'subgraph "quoted title"', 'subgraph two words', 'subgraph', 'subgraph ' + pick(IDS) + '["`md`"]']);
    return `${head}\n${inner.join('\n')}\nend`;
  }
  return nodes();
}

const lines = [];
const count = Number(process.argv[3] ?? 500);
for (let c = 0; c < count; c++) {
  let src = pick(['graph TD', 'flowchart LR', 'graph BT\n', 'flowchart RL;', 'graph', 'graph TB', 'flowchart-elk TD', 'graph >', 'graph v', 'graph <', 'graph ^']) + pick(['\n', ';\n', '\n  ']);
  for (let i = ri(1, 8); i > 0; i--) src += statement(0) + pick(['\n', ';\n', '\n', '\n  ', ' ;\n']);
  if (rnd() < 0.25 && src.length > 3) { const at = ri(0, src.length - 1); src = rnd() < 0.5 ? src.slice(0, at) + src.slice(at + 1) : src.slice(0, at) + pick(['"', '[', ']', '(', ')', '{', '}', '|', '-', '>', '@', ' ', '\n', '&', 'end', 'subgraph ']) + src.slice(at); }
  if (rnd() < 0.5) src += pick(['linkStyle 0 stroke:red', 'linkStyle 0,1 stroke:blue,stroke-width:2px', 'linkStyle 0 interpolate basis', 'linkStyle 0 interpolate step stroke:red', 'linkStyle 99 stroke:red', 'click A callback', 'accTitle: late']) + '\n';
  const opts = pick([{}, {}, {}, {}, {}, { inheritDir: true }, { inheritDir: true, direction: 'LR' }, ...(rnd() < 0.1 ? [{ maxEdges: 3 }] : [])]);
  const db = new P.FlowDb(opts);
  let res;
  try { P.parseFlowchart(src, db); res = ['ok', ...dump(db)]; } catch (e) { res = ['err ' + hex16(String(e.message)) + ' ' + hex16(e.code ?? ''), ...dump(db).length ? [] : []]; }
  lines.push(`case ${hex16(src)} ${JSON.stringify(opts).replace(/\s/g, '')} ${res.length}`);
  lines.push(...res);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/parser_cases.txt', lines.join('\n') + '\n');
console.log('cases', count, 'lines', lines.length);
