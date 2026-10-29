// Renders random treemaps with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_treemap_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const NAMES = ['Category A', 'Category B', 'Item A1', 'Item A2', 'Item B1', 'Alpha', 'Beta', 'Gamma', 'Delta', 'A much longer name that will not fit its box', '日本語', 'é ü ñ', 'Fix <b>bold</b> &amp; more', 'x', 'Two words', "it's", '😀 emoji', 'Revenue', 'Cost', 'Profit', 'H<sub>2</sub>O and x<sup>2</sup>', '<u>under</u> and <code>mono code</code>', 'long\u00adword\u00adwith\u00adhints that wraps', 'List<T> & <foo>'];
const SAFE = ['10', '20', '30', '5', '100', '1', '0', '7', '15', '40', '60', '2', '1000', '12.5', '0.5', '3.14159', '250', '9', '33', '1,000', '1,234,567', '0.001', '99999', '123456.789'];
const ODD = ['1_000', '.5', '1.2.3', '..5', ',,', '1e3', '007', '5.', '99999999999999999999999', '0.0000001', '-5', '-0'];
const SPECS = [',', '$0,0', '$,', ',.0f', '.2f', '.1%', '.0%', 'd', ',d', '.3e', '.2~f', '~', '$.2f', '+', '+,.1f', '(,.2f', ' ,', '.4', '.1', '.12', '.20f', '.25f', '08.2f', '*>10,.2f', '<10', '^,', 'x', 'garbage', '', '.f', '$', ',.2~f', 's', '.0e', '.3~e'];
const COLORS = ['#f9f', '#FFF', '#123', '#1234', '#12345678', '#ffeeaa', '#003366', 'red', 'RED', 'navy', 'grey', 'lightgrey', 'gold', 'black', 'white', 'transparent', 'none', 'rgb(0,0,0)', 'rgb(255, 255, 200)', 'rgba(10,20,30,0.4)', 'rgb(100% 0% 0% / 50%)', 'rgb(0 128 0)', 'rgb(10%, 20%, 30%)', 'hsl(210, 60%, 30%)', 'hsl(50deg 90% 60%)', 'hsla(0.5turn, 50%, 50%, 0.9)', 'hsl(120 none 50%)', 'var(--c)', 'rgb(1,2)', 'foo', '#gg0000', 'calc(1+2)', 'rgb(300,300,300)'];
const STYLES = [() => `fill:${COLORS[Math.floor(rnd() * COLORS.length)]}`, () => `fill:${COLORS[Math.floor(rnd() * COLORS.length)]};fill-opacity:0.${1 + Math.floor(rnd() * 9)}`, () => `opacity:0.${1 + Math.floor(rnd() * 9)},fill:${COLORS[Math.floor(rnd() * COLORS.length)]}`, 'fill:#ff0000', 'fill:#00f,stroke:#000', 'stroke:red;stroke-width:3', 'color:#fff', 'font-weight:bold,fill:#ccc', 'fill:red;', 'a\\,b:c', '', 'fill:blue,,color:white', 'font-size:12px', 'font-size:20px;fill:#ccc', 'font-size:8px,font-style:italic', 'font-style:italic', 'font-weight:bold'];
const style = () => { const x = pick(STYLES); return typeof x === 'function' ? x() : x; };
const CLASSES = ['hot', 'cold', 'big', 'x_y', 'a1'];

function node(indent, forceLeaf) {
  const name = `"${pick(NAMES).replace(/"/g, '')}"`;
  const cls = rnd() < 0.18 ? `:::${pick(CLASSES)}` : '';
  if (forceLeaf || rnd() < 0.6) {
    const sep = pick([':', ':', ',', ' :', ': ', ' , ']);
    const v = rnd() < 0.985 ? pick(SAFE) : pick(ODD);
    return ' '.repeat(indent) + name + sep + v + cls;
  }
  return ' '.repeat(indent) + name + cls;
}

// Many small nested sections: headerless and short-headed boxes, small corners.
function dense() {
  const lines = [];
  for (let r = ri(2, 5); r > 0; r--) {
    lines.push(`  "${pick(NAMES).replace(/"/g, '')}"${rnd() < 0.2 ? ':::' + pick(CLASSES) : ''}`);
    for (let g = ri(1, 4); g > 0; g--) {
      lines.push(`    "${pick(NAMES).replace(/"/g, '')}"${rnd() < 0.2 ? ':::' + pick(CLASSES) : ''}`);
      for (let l = ri(2, 9); l > 0; l--) lines.push(`      "${pick(NAMES).replace(/"/g, '')}": ${pick(SAFE)}${rnd() < 0.2 ? ':::' + pick(CLASSES) : ''}`);
    }
    for (let l = ri(0, 4); l > 0; l--) lines.push(`    "${pick(NAMES).replace(/"/g, '')}": ${pick(SAFE)}`);
  }
  if (rnd() < 0.6) for (const c of CLASSES.slice(0, ri(1, 4))) lines.push(`  classDef ${c} ${style()}`);
  return lines;
}

function body() {
  if (rnd() < 0.3) return dense();
  const lines = [];
  const base = pick([0, 0, 2, 4]);
  const roots = ri(0, 4);
  for (let r = 0; r < roots; r++) {
    lines.push(node(base, rnd() < 0.15));
    const kids = ri(0, 5);
    for (let k = 0; k < kids; k++) {
      const ind = base + ri(1, 2) * 2;
      lines.push(node(ind, false));
      if (rnd() < 0.5) for (let g = ri(1, 4); g > 0; g--) lines.push(node(ind + ri(1, 2) * 2, rnd() < 0.8));
    }
  }
  if (rnd() < 0.35) for (const c of CLASSES.slice(0, ri(1, 4))) lines.push(`  classDef ${c} ${style()}`);
  if (rnd() < 0.02) lines.push(pick(['  %% comment', '', '   ', '"orphan"', 'foo', '  "x" :', '  "x" : :', '  :::cls', '  "a" :::']));
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_treemap';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(NAMES).replace(/[:#<&]/g, '')}\n---\n`;
  if (rnd() < 0.55) {
    const cfg = {};
    if (rnd() < 0.6) cfg.valueFormat = pick(SPECS);
    if (rnd() < 0.2) cfg.showValues = pick([false, true, 'no', 0]);
    if (rnd() < 0.25) cfg.padding = pick([0, 2, 4, 10, 300, -1]);
    if (rnd() < 0.2) cfg.nodeWidth = pick([20, 40, 64, 100, 500, 0]);
    if (rnd() < 0.2) cfg.nodeHeight = pick([20, 40, 80, 500, 0]);
    if (rnd() < 0.15) cfg.diagramPadding = pick([0, 4, 20, 3000]);
    src += `%%{init: {"treemap": ${JSON.stringify(cfg)}}}%%\n`;
  }
  src += pick(['treemap-beta', 'treemap-beta', 'treemap', 'treemap-beta  ', '\n\ntreemap-beta', '%% c\ntreemap']) + '\n';
  if (rnd() < 0.2) src += `  title ${pick(NAMES).replace(/[<&]/g, '')}\n`;
  if (rnd() < 0.08) src += '  accTitle: Treemap acc\n';
  if (rnd() < 0.08) src += '  accDescr: A treemap description\n';
  for (const line of body()) src += line + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.015 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', ':', ',', '\n', ' ', '%', '#']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.4) options.maxWidth = pick([200, 320, 400, 500, 640, 800, 1200, 60]);
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
