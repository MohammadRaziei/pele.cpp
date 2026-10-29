// Renders random flowcharts with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp. Each case is one file pair: <dir>/N.mmd and <dir>/N.svg (or N.err).
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const IDS = ['A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'node1', 'start', 'end1', 'é', '日本'];
const TEXTS = ['text', 'Two words', '"quoted"', '"Long label that will need to wrap across several lines of text"', '"`**bold** and *it*`"', 'a<br/>b', 'x&amp;y', '日本語', 'Yes', 'No', 'Hello World', '<b>b</b> i', 'fa:fa-car Car', 'H<sub>2</sub>O', 'x<sup>2</sup> <u>u</u>', '<code>mono</code> text', '"long\u00adword\u00adhere and more words to wrap"'];
const SHAPES = [(t) => `[${t}]`, (t) => `[${t}]`, (t) => `(${t})`, (t) => `(${t})`, (t) => `((${t}))`, (t) => `(((${t})))`, (t) => `{${t}}`, (t) => `{{${t}}}`, (t) => `[[${t}]]`, (t) => `[(${t})]`, (t) => `([${t}])`, (t) => `>${t}]`, (t) => `[/${t}/]`, (t) => `[\\${t}\\]`, (t) => `[/${t}\\]`, (t) => `[\\${t}/]`, (t) => `(-${t}-)`];
const LINKS = ['-->', '-->', '-->', '---', '-.->', '-.-', '==>', '===', '~~~', '--x', '--o', 'x--x', 'o--o', '<-->', '--->', '---->', '-..->'];
const METAS = ['@{ shape: rect }', '@{ shape: cyl, label: "db" }', '@{ shape: doc }', '@{ shape: hex }', '@{ shape: diam }', '@{ shape: circle }', '@{ shape: fork }', '@{ shape: bolt }', '@{ shape: stadium, label: "pill" }', '@{ shape: lean-r }', '@{ shape: trap-b }', '@{ shape: tri }', '@{ shape: cloud }', '@{ shape: bang }', '@{ shape: sl-rect }', '@{ shape: h-cyl }', '@{ shape: st-rect }'];

const nid = () => pick(IDS);
const node = () => nid() + (rnd() < 0.55 ? pick(SHAPES)(pick(TEXTS)) : '') + (rnd() < 0.08 ? ':::cls' : '');
const nodes = () => (rnd() < 0.15 ? `${node()} & ${node()}` : node());
function statement(depth) {
  const k = ri(0, 14);
  if (k <= 6) return `${nodes()} ${pick(LINKS)} ${nodes()}`;
  if (k === 7) return `${nodes()} ${pick(LINKS)}|${pick(TEXTS)}| ${nodes()}`;
  if (k === 8) return `${nodes()} -- ${pick(TEXTS)} --> ${nodes()}`;
  if (k === 9) return `${nid()} ${pick(LINKS)} ${nid()} ${pick(LINKS)} ${nid()}`;
  if (k === 10) return `${nid()}${pick(METAS)}`;
  if (k === 11) return `${nid()} --> ${nid()}`;
  if (k === 12 && depth < 3) {
    const inner = [];
    for (let i = ri(1, 4); i > 0; i--) inner.push(statement(depth + 1));
    if (rnd() < 0.3) inner.push(pick(['direction TB', 'direction LR', 'direction RL', 'direction BT']));
    return `subgraph ${pick(['s' + ri(1, 4), 's' + ri(1, 4) + '[Title ' + ri(1, 9) + ']', '"quoted title"'])}\n${inner.join('\n')}\nend`;
  }
  if (k === 13) return `${nid()} --> ${nid()}`;
  return `${nid()} ${pick(LINKS)} ${nid()}`;
}

const COLORS = ['#f9f', '#FFF', '#123', '#1234', '#12345678', '#ffeeaa', '#003366', 'red', 'RED', 'navy', 'grey', 'lightgrey', 'gold', 'black', 'white', 'transparent', 'none', 'rgb(0,0,0)', 'rgb(255, 255, 200)', 'rgba(10,20,30,0.4)', 'rgb(100% 0% 0% / 50%)', 'rgb(0 128 0)', 'rgb(10%, 20%, 30%)', 'hsl(210, 60%, 30%)', 'hsl(50deg 90% 60%)', 'hsla(0.5turn, 50%, 50%, 0.9)', 'hsl(120 none 50%)', 'var(--c)', 'rgb(1,2)', 'foo', '#gg0000', 'calc(1+2)', 'rgb(300,300,300)'];
const OPACITIES = ['', '', '', ',fill-opacity:0.3', ',opacity:0.6', ',fill-opacity:0.5', ',opacity:0.4,fill-opacity:0.9', ',fill-opacity:calc(1/2)', ',opacity:50%', ',opacity:0'];
const LINES = ['stroke:red', 'stroke:#fff', 'stroke-width:4px', 'stroke:none', 'stroke:transparent', 'stroke-width:calc(1px + 1px)', 'opacity:0.5', 'stroke-opacity:0.4', 'stroke:blue,stroke-width:3px,opacity:0.7', 'stroke-width:1px', 'stroke-width:0.5', 'stroke-width:2.5px'];
// Subgraphs that set a fill, with edges inside, crossing their border, and styled.
function filled() {
  const out = [];
  const n = ri(1, 3);
  for (let g = 1; g <= n; g++) {
    out.push(`subgraph g${g}[${pick(['Group', 'A longer group title here', 'G'])} ${g}]`);
    if (rnd() < 0.25) out.push(pick(['direction LR', 'direction TB']));
    const k = ri(2, 5);
    for (let i = 1; i <= k; i++) out.push(`  n${g}${i}${pick(['', '[Box]', '(Round)', '{Diamond}', '((Circle))', '>Flag]', '[(DB)]'])} ${pick(['-->', '---', '-.->', '==>', '--o', '--x'])}${rnd() < 0.2 ? '|lbl|' : ''} n${g}${(i % k) + 1}`);
    if (g > 1 && rnd() < 0.6) out.push(`  n${g}1 --> n${g - 1}2`);
    out.push('end');
  }
  for (let i = ri(0, 3); i > 0; i--) out.push(`out${i} ${pick(['-->', '-.->'])} n1${ri(1, 2)}`, `n1${ri(1, 2)} --> out${i}`);
  for (let g = 1; g <= n; g++) if (rnd() < 0.8) out.push(`style g${g} fill:${pick(COLORS)}${pick(OPACITIES)}${rnd() < 0.3 ? ',stroke:' + pick(COLORS) : ''}${rnd() < 0.2 ? ',color:' + pick(COLORS) : ''}`);
  for (let i = ri(0, 4); i > 0; i--) out.push(`style n${ri(1, n)}${ri(1, 3)} fill:${pick(COLORS)}${pick(OPACITIES)}${rnd() < 0.3 ? ',color:' + pick(COLORS) : ''}`);
  if (rnd() < 0.3) out.push(`classDef c1 fill:${pick(COLORS)}`, `class g1 c1`);
  for (let i = ri(0, 4); i > 0; i--) out.push(`linkStyle ${ri(0, 8)} ${pick(LINES)}`);
  if (rnd() < 0.15) out.push(`linkStyle default ${pick(LINES)}`);
  return out.map((l) => '  ' + l).join('\n') + '\n';
}

const dir = process.argv[4] ?? '/tmp/e2e';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0;
for (let c = 0; c < count; c++) {
  let src = pick(['flowchart TD', 'graph TD', 'flowchart LR', 'flowchart RL', 'flowchart BT', 'graph LR', 'flowchart TB']) + '\n';
  if (rnd() < 0.15) src = `---\ntitle: ${pick(['My Chart', 'A longer title for the chart'])}\n---\n` + src;
  if (rnd() < 0.12) src = `%%{init: {"flowchart": {"curve": "${pick(['basis', 'linear', 'step', 'stepBefore', 'cardinal'])}"}}}%%\n` + src;
  if (rnd() < 0.4) src += filled();
  else for (let i = ri(1, 9); i > 0; i--) src += '  ' + statement(0) + '\n';
  if (rnd() < 0.25) src += pick(['  style A fill:#f9f,stroke:#333,stroke-width:4px', '  classDef cls fill:#ff0,color:#000', '  linkStyle 0 stroke:red,stroke-width:3px', '  linkStyle default stroke:green', '  click A "http://x.y" "tip"', '  A:::cls', '  accTitle: Chart title', '  accDescr: A description']) + '\n';
  if (rnd() < 0.06 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '[', ']', '(', ')', '{', '}', '|', '-', '>', ' ']) + src.slice(at); }
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err);
