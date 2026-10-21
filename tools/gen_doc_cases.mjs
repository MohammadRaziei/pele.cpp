// Runs Pele's svg/root.ts, svg/title.ts and svg/cluster.ts on random input.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { svgDocument } from './svg/root.ts'; export { withTitle } from './svg/title.ts'; export * from './svg/cluster.ts'; export { layoutLabel } from './text/label.ts'; export { metricsMeasurer } from './text/measurer.ts'; export { cnode } from './layout/compound.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/doc_bundle.mjs', out.outputFiles[0].text);
const D = await import('/tmp/doc_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');
const f = (v) => (v === undefined ? 'undefined' : Object.is(v, -0) ? '0' : String(v));
const r2 = (v) => v.toString();

const R = ' rx="var(--pele-radius,4px)"';
const PIECES = [
  `<rect x="1" y="2" width="3" height="4"${R}/>`, `<rect x="1"${R} style="fill:red" width="3"/>`, `<rect class="a"${R}>`, '<rect x="1" width="2"/>', `<rectangle${R}/>`, '<g>', '</g>',
  '<text font-weight="var(--_w)">x</text>', '<text font-weight="var(--_tw)">t</text>', '<text font-weight="var(--_hw)">h</text>', 'var(--_r)', 'var(--_rw)', 'var(--_x)', '<path d="M0,0L1,1"/>',
  `<rect${R} x="5"/>`, `<rect x="9"${R}   />`, '<rect', '<rect\nx="1"\n/>', 'plain text',
];
const lines = [];
const count = Number(process.argv[3] ?? 600);
for (let c = 0; c < count; c++) {
  const k = c % 3;
  if (k === 0) {
    let content = '';
    for (let i = ri(0, 6); i > 0; i--) content += pick(PIECES);
    const type = pick(['flowchart', 'pie', 'sequence']);
    const w = pick([100, 250.5, 1e3]), h = pick([50, 77, 300.25]), fs_ = pick([12, 14, 16]);
    const options = { idPrefix: pick([undefined, 'x', 'a&b']), responsive: pick([undefined, true, false]), fontFamily: pick([undefined, 'Arial', 'bad;font']), fontFamilyMono: pick([undefined, 'Fira Code']) };
    const acc = { accTitle: pick([undefined, '', 'A title', 'A & B']), accDescr: pick([undefined, 'desc <x>']) };
    const svg = D.svgDocument(type, w, h, fs_, options, acc, content);
    lines.push(`doc ${type} ${w} ${h} ${fs_} ${options.idPrefix === undefined ? '-' : hex8(options.idPrefix)} ${f(options.responsive)} ${options.fontFamily === undefined ? '-' : hex8(options.fontFamily)} ${options.fontFamilyMono === undefined ? '-' : hex8(options.fontFamilyMono)} ${acc.accTitle === undefined ? '-' : hex16(acc.accTitle)} ${acc.accDescr === undefined ? '-' : hex16(acc.accDescr)} ${hex8(content)} ${hex8(svg)}`);
  } else if (k === 1) {
    const title = pick([undefined, '', 'Title', 'A longer diagram title here', '<b>bold</b> title', '日本語タイトル']);
    const size = pick([14, 16, 18]), pad = pick([8, 16, 20]), w = pick([100, 300, 200.5]), h = pick([60, 120]);
    const content = pick(['<g/>', '<rect/>']);
    const t = D.withTitle(title, D.metricsMeasurer, size, pad, w, h, content);
    lines.push(`title ${title === undefined ? '-' : hex16(title)} ${size} ${pad} ${w} ${h} ${hex8(content)} ${t.width} ${t.height} ${hex8(t.content)}`);
  } else {
    const crossings = new Map();
    const routes = [];
    for (let i = ri(0, 4); i > 0; i--) {
      const route = [];
      for (let j = ri(2, 4); j > 0; j--) route.push(ri(0, 300) + pick([0, 0.5]), pick([20, 20.4, 20.5, 50, ri(0, 100)]), pick([0, 1]));
      routes.push(route);
      D.markCrossings(crossings, route);
    }
    const node = D.cnode(pick([100, 200, 150.5]), pick([100, 60]));
    node.x = ri(50, 250); node.y = pick([70, 50, 20.9, 70.4]);
    const width = pick([0, 40, 80.5]), pad = pick([8, 16]);
    const tx = D.clusterTitleX(node, width, crossings, pad);
    const xtest = pick([tx, tx + 3, node.x]);
    const crossed = D.titleCrossed(node, width, xtest, crossings);
    const depth = ri(0, 9);
    const nodes = [];
    for (let i = 0; i <= depth; i++) nodes.push({ parent: i < depth ? i + 1 : -1 });
    const label = D.layoutLabel('Cluster', false, D.metricsMeasurer, 12, 200, 0);
    const struck = D.struckTitle(label, 10.5, 20, ' class="t"', 'c1', nodes, 0);
    lines.push(`cluster ${routes.length} ${routes.map((r) => r.length + ' ' + r.map(f).join(' ')).join(' ')} ${node.w} ${node.h} ${node.x} ${node.y} ${width} ${pad} ${f(tx)} ${xtest} ${+crossed} ${depth} ${hex8(struck)}`);
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/doc_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
