// Renders random class diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_class_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const dir = process.argv[4] ?? '/tmp/class_cases';
fs.mkdirSync(dir, { recursive: true });
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const NAMES = ['Animal', 'Dog', 'Duck', 'Fish', 'Zebra', 'Shape', 'A', 'B', 'C', 'D', 'E', 'Order', 'Customer', 'Item', 'Node', 'x_1', 'é', '日本', 'Cat', 'Bird', 'LongClassNameThatIsWide', 'Repository'];
const MEMBERS = ['+int age', '-String name', '#double w', '~bool ok', '+swim()', '-run(int a) int', '+abstractM()*', '+staticM()$', 'List~int~ items', '+get(a, b) List~int~', 'plain', '+String id', 'x()', '+very long member declaration that makes the box wide(int a, int b) String', 'H<sub>2</sub>O', '+日本語 name', 'a &amp; b'];
const REL = ['<|--', '--|>', '*--', '--*', 'o--', '--o', '<--', '-->', '..>', '<..', '..|>', '<|..', '--', '..', '()--', '--()'];
const COLORS = ['#f9f', '#FFF', '#123', '#1234', '#12345678', '#ffeeaa', '#003366', 'red', 'RED', 'navy', 'grey', 'lightgrey', 'gold', 'black', 'white', 'transparent', 'none', 'rgb(0,0,0)', 'rgb(255, 255, 200)', 'rgba(10,20,30,0.4)', 'rgb(100% 0% 0% / 50%)', 'rgb(0 128 0)', 'rgb(10%, 20%, 30%)', 'hsl(210, 60%, 30%)', 'hsl(50deg 90% 60%)', 'hsla(0.5turn, 50%, 50%, 0.9)', 'hsl(120 none 50%)', 'var(--c)', 'rgb(1,2)', 'foo', '#gg0000', 'calc(1+2)', 'rgb(300,300,300)'];
const STYLES = [() => `fill:${COLORS[Math.floor(rnd() * COLORS.length)]}`, () => `fill:${COLORS[Math.floor(rnd() * COLORS.length)]};fill-opacity:0.${1 + Math.floor(rnd() * 9)}`, () => `opacity:0.${1 + Math.floor(rnd() * 9)},fill:${COLORS[Math.floor(rnd() * COLORS.length)]}`, 'font-family:serif', 'font-family:"Fira Code"', 'fill:#f9f', 'stroke:#333,stroke-width:2px', 'color:#fff', 'font-style:italic', 'fill:red;stroke:blue', 'font-size:20px', 'font-weight:bold', 'stroke-dasharray: 5 5', 'rx:8'];
const style = () => { const x = pick(STYLES); return typeof x === 'function' ? x() : x; };
const CARD = ['1', '*', '0..1', 'many', 'n', '1..*', '&lt;x&gt;', 'a long cardinality'];
const LABELS = ['uses', 'owns', 'has a', 'a longer relation label here', '日本', 'x<br/>y', ''];
const NOTES = ['a note', 'two\\nlines', 'x', 'a much longer note text that has to wrap around the note box width'];

function statement(depth = 0) {
  const a = pick(NAMES), b = pick(NAMES);
  switch (ri(0, 20)) {
    case 0: return `class ${a}`;
    case 1: return `class ${a} {\n${Array.from({ length: ri(0, 5) }, () => '  ' + pick(MEMBERS)).join('\n')}\n}`;
    case 2: case 3: return `${a} ${pick(REL)} ${b}`;
    case 4: return `${a} "${pick(CARD)}" ${pick(REL)} "${pick(CARD)}" ${b} : ${pick(LABELS)}`;
    case 5: return `${a} ${pick(REL)} ${b} : ${pick(LABELS)}`;
    case 6: return `${a} : ${pick(MEMBERS)}`;
    case 7: return `class ${a} {\n  <<${pick(['interface', 'service', 'abstract', 'a longer annotation'])}>>\n  ${pick(MEMBERS)}\n}`;
    case 8: return `note "${pick(NOTES)}"`;
    case 9: return `note for ${a} "${pick(NOTES)}"`;
    case 10: return `classDef ${pick(['hot', 'cold', 'big'])} ${style()}${rnd() < 0.4 ? ',' + style() : ''}`;
    case 11: return `class ${a}:::${pick(['hot', 'cold', 'big'])}`;
    case 12: return `style ${a} ${style()}`;
    case 13: return `${a} ${pick(REL)} ${a} : ${pick(LABELS)}`;
    case 14: return `namespace ${pick(['Shapes', 'Core', 'A.B', 'N', 'Outer.Inner'])} {\n  class ${a}\n  class ${b}\n${rnd() < 0.3 ? '  note "in ns"\n' : ''}}`;
    case 15: return `link ${a} "https://example.com/${pick(['x', 'a&amp;b', ''])}" "tip"${rnd() < 0.5 ? ' _blank' : ''}`;
    case 16: return `click ${a} href "${pick(['https://x.org', 'javascript:alert(1)', '/rel'])}" "tip"${rnd() < 0.3 ? ' _top' : ''}`;
    case 17: return `class ${a}["Label of ${b}"]`;
    case 18: return `class ${a}~T~ {\n  ${pick(MEMBERS)}\n  ${pick(MEMBERS)}\n}`;
    case 19: return `${a}~T~ <|-- ${b}`;
    default: return `${a} : ${pick(MEMBERS)}\n${a} : ${pick(MEMBERS)}\n${a} ${pick(REL)} ${b}`;
  }
}

const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.1) src += `---\ntitle: ${pick(['Class title', 'x', 'A <b>b</b>'])}\n---\n`;
  const cfg = {};
  if (rnd() < 0.15) cfg.hierarchicalNamespaces = pick([false, true]);
  if (rnd() < 0.15) cfg.hideEmptyMembersBox = pick([true, false]);
  if (rnd() < 0.1) cfg.nodeSpacing = pick([20, 80, 3000, -1]);
  if (rnd() < 0.1) cfg.rankSpacing = pick([20, 100, 3000]);
  if (Object.keys(cfg).length) src += `%%{init: {"class": ${JSON.stringify(cfg)}}}%%\n`;
  src += pick(['classDiagram', 'classDiagram', 'classDiagram-v2']) + '\n';
  if (rnd() < 0.45) src += `  direction ${pick(['TB', 'LR', 'RL', 'BT'])}\n`;
  if (rnd() < 0.05) src += '  accTitle: class acc\n';
  if (rnd() < 0.35) {
    // Namespaces that edges cross into, from all sides.
    const inside = ['P', 'Q', 'R', 'S'].slice(0, ri(2, 4)), outside = ['X', 'Y', 'Z', 'W'].slice(0, ri(1, 4));
    const nsName = pick(['Grp', 'A very long namespace title that is wide', 'N', 'Outer.Inner']);
    src += `namespace ${nsName} {\n${inside.map((c) => '  class ' + c).join('\n')}\n}\n`;
    if (rnd() < 0.4) src += `namespace Other {\n  class ${pick(outside)}\n}\n`;
    const all = inside.concat(outside);
    for (let e = ri(3, 9); e > 0; e--) src += `${pick(all)} ${pick(['-->', '<|--', '..>', '*--', 'o--'])} ${pick(all)}${rnd() < 0.3 ? ' : ' + pick(LABELS) : ''}\n`;
  }
  if (rnd() < 0.3) {
    // Classes with fills of their own: the text color follows the fill.
    const names = ['K1', 'K2', 'K3'];
    for (const k of names) src += `class ${k} {\n  +int v\n  +run()\n}\n`;
    src += `K1 --> K2\nK2 <|-- K3\n`;
    src += `classDef paint ${style()}${rnd() < 0.4 ? ',' + style() : ''}\n`;
    src += `cssClass "${names.slice(0, ri(1, 3)).join(",")}" paint\n`;
    if (rnd() < 0.4) src += `style K3 ${style()}\n`;
  }
  const n = ri(0, rnd() < 0.2 ? 20 : 8);
  for (let i = 0; i < n; i++) src += (rnd() < 0.3 ? '  ' : '') + statement() + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.02 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '{', '}', '~', ':', '<<', '--', '\n', '[']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.2) options.maxWidth = pick([200, 320, 480, 640, 1000]);
  if (rnd() < 0.1) options.fontSize = pick([10, 12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  if (rnd() < 0.2) options.monospace = pick([['members'], ['members', 'attributes'], ['messages']]);
  if (rnd() < 0.2) options.idPrefix = pick(['a', 'mermaid-1', 'é日本']);
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  if (Object.keys(options).length) { fs.writeFileSync(optPath, Object.entries(options).map(([k, v]) => `${k}=${Array.isArray(v) ? v.join(',') : v}`).join('\n') + '\n'); withOpts++; }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, options).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', withOpts);
