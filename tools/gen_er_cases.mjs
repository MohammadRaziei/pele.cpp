// Renders random ER diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_er_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const dir = process.argv[4] ?? '/tmp/er_cases';
fs.mkdirSync(dir, { recursive: true });
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const NAMES = ['CUSTOMER', 'ORDER', 'LINE-ITEM', 'PRODUCT', 'A', 'B', 'C', 'D', 'E', 'Category', 'x_1', 'é', '日本', 'Supplier', 'Invoice', 'LongEntityNameThatIsRatherWide', '42', '3.5', 'a-b', 'a.b'];
const ODD_NAMES = ['1', 'ONE', 'to', 'u', 'many', 'o', 'end', 'style', 'class'];
const QUOTED = ['"Order Item"', '"a b"', '"x:y"', '"日本 語"', '"with <b>bold</b>"', '"a&amp;b"', '"[x]"'];
const ALIASES = ['"The customer"', '"Alias with <i>tags</i>"', 'Alias', '"A very long alias that will surely have to wrap around somewhere"', '"x<br/>y"'];
const L = ['||', '|o', '}o', '}|', 'o|', 'o{', '|{', 'one', 'only one', 'zero or one', 'zero or more', 'zero or many', 'one or more', 'one or many', '1+', '0+', 'many(0)', 'many(1)', 'many', '1', 'u', 'o|', '||'];
const R = ['||', 'o|', 'o{', '|{', '|o', '}o', '}|', 'one', 'only one', 'zero or one', 'zero or more', 'zero or many', 'one or more', 'one or many', '1+', '0+', 'many(0)', 'many(1)', 'many', 'u', '||', 'o{'];
const IDENT = ['--', '..', '-.', '.-', 'to', 'optionally to', '--', '..'];
const ROLES = ['places', 'contains', 'is in', '"is part of"', '"a longer relationship role label here"', '日本', '"x<br/>y"', '""', '"has"', 'owns', '"a & b"'];
const TYPES = ['string', 'int', 'float', 'varchar(255)', 'List~int~', 'Map~string,int~', 'type?', 'datetime', 'bool', 'uuid', 'Array~List~int~~', 'é', '`odd type`', 'x[]', 'decimal(10,2)'];
const ANAMES = ['id', 'name', 'age', 'created_at', 'customer_id', 'a_very_long_attribute_name_here', 'x', 'é日', 'ids', 'total*', 'n1'];
const KEYS = ['PK', 'FK', 'UK', 'PK, FK', 'FK, UK', 'PK, FK, UK', 'pk', 'Fk'];
const COMMENTS = ['"the primary key"', '"a comment"', '"a rather long comment that goes on and on and has to wrap somewhere along its way"', '"日本"', '"x<br/>y"', '"<b>b</b> and `code`"', '"a & b"'];
const COLORS = ['#f9f', '#FFF', '#123', '#1234', '#12345678', '#ffeeaa', '#003366', 'red', 'navy', 'grey', 'lightgrey', 'gold', 'black', 'white', 'transparent', 'none', 'foo', '#gg0000', 'RED', 'yellow', '#000', '#fff', 'orange'];
const RARE_COLORS = ['rgb(0,0,0)', 'rgb(255, 255, 200)', 'rgba(10,20,30,0.4)', 'hsl(210, 60%, 30%)', 'var(--c)'];
const color = () => (rnd() < 0.05 ? pick(RARE_COLORS) : pick(COLORS));
const STYLES = [() => `fill:${color()}`, () => `fill:${color()},stroke:${color()}`, () => `fill:${color()},fill-opacity:${pick([0, 1])}`, 'font-family:serif', 'font-family:monospace', 'stroke:#333,stroke-width:2px', 'color:#fff', 'font-style:italic', 'font-weight:bold', 'font-size:20px', 'stroke-dasharray:5', 'rx:8', 'color:red'];
const style = () => { const x = pick(STYLES); return typeof x === 'function' ? x() : x; };
const name = () => (rnd() < 0.08 ? pick(QUOTED) : rnd() < 0.04 ? pick(ODD_NAMES) : pick(NAMES));
const CLASSES = ['hot', 'cold', 'big', 'internal-link', 'x'];

function attribute() {
  let s = `${pick(TYPES)} ${pick(ANAMES)}`;
  if (rnd() < 0.35) s += ' ' + pick(KEYS);
  if (rnd() < 0.3) s += ' ' + pick(COMMENTS);
  return s;
}
function block(n = ri(0, 5)) {
  const sep = pick(['\n', '\n', '\n', ' ', ',']);
  return `{\n${Array.from({ length: n }, () => '    ' + attribute()).join(sep === ',' ? '\n' : '\n')}\n  }`;
}
const KW = ['one', 'only one', 'zero or one', 'zero or more', 'zero or many', 'one or more', 'one or many', 'many(0)', 'many(1)', '1+', '0+', 'many'];
function relation(a, b) {
  const x = rnd();
  if (x < 0.04) return `${a} u${pick(['--', '..', '-.'])}${pick(['||', 'o{', 'o|'])} ${b} : ${pick(ROLES)}`;
  if (x < 0.72) return `${a} ${pick(['||', '|o', '}o', '}|'])}${pick(['--', '..'])}${pick(['||', 'o|', 'o{', '|{'])} ${b} : ${pick(ROLES)}`;
  if (x < 0.9) return `${a} ${pick(KW)} ${pick(['to', 'optionally to'])} ${pick(KW)} ${b} : ${pick(ROLES)}`;
  const ca = pick(L), cb = pick(R), id = pick(IDENT);
  return `${a} ${ca} ${id} ${cb} ${b} : ${pick(ROLES)}`;
}

function statement(names) {
  const nm = () => (names && rnd() < 0.8 ? pick(names) : name());
  const a = nm(), b = nm();
  switch (ri(0, 17)) {
    case 0: return a;
    case 1: return `${a} ${block()}`;
    case 2: case 3: case 4: return relation(a, b);
    case 5: return relation(a, a);
    case 6: return `${a}[${pick(ALIASES)}]${rnd() < 0.5 ? ' ' + block() : ''}`;
    case 7: return `${a}:::${pick(CLASSES)}`;
    case 8: return `style ${a} ${style()}${rnd() < 0.3 ? ',' + style() : ''}`;
    case 9: return `classDef ${pick(CLASSES)} ${style()}${rnd() < 0.4 ? ',' + style() : ''}`;
    case 10: return `class ${a}${rnd() < 0.3 ? ',' + b : ''} ${pick(CLASSES)}`;
    case 11: return `${a}:::${pick(CLASSES)} ${pick(['||', '}o'])}--${pick(['o{', '||'])} ${b}:::${pick(CLASSES)} : ${pick(ROLES)}`;
    case 12: return `${a} ${block(ri(1, 6))}\n  ${a} ${block(ri(1, 3))}`;
    case 13: return rnd() < 0.5 ? `direction ${pick(['TB', 'LR', 'BT', 'RL'])}` : `accDescr: ${pick(['d', 'a description'])}`;
    case 14: return `${a}:::${pick(CLASSES)} ${block(ri(1, 4))}`;
    case 15: return `accTitle: ${pick(['t', 'a title'])}`;
    case 16: return `${a} {\n  ${attribute()}\n  ${attribute()}\n}`;
    default: return relation(a, b);
  }
}

function subgraph(depth, names) {
  const id = pick(['G', 'Grp', 'Sub1', 'S', 'ORDER', 'CUSTOMER', '"Quoted Group"', 'é']);
  const title = rnd() < 0.5 ? `[${pick(['A title', 'Longer group title here', 'x y z', '"quoted"', 'ORDER'])}]` : '';
  let s = `subgraph ${id}${title}\n`;
  if (rnd() < 0.3) s += `  direction ${pick(['TB', 'LR', 'BT', 'RL'])}\n`;
  for (let i = ri(0, 4); i > 0; i--) s += '  ' + statement(names) + '\n';
  if (depth < 2 && rnd() < 0.3) s += subgraph(depth + 1, names).split('\n').map((l) => '  ' + l).join('\n') + '\n';
  return s + 'end';
}

const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.15) src += `---\ntitle: ${pick(['ER title', 'x', 'A <b>b</b>', 'A long diagram title that is wide'])}\n---\n`;
  const cfg = {};
  if (rnd() < 0.1) cfg.nodeSpacing = pick([20, 80, 3000, -1]);
  if (rnd() < 0.1) cfg.rankSpacing = pick([20, 100, 3000]);
  if (Object.keys(cfg).length) src += `%%{init: {"er": ${JSON.stringify(cfg)}}}%%\n`;
  src += 'erDiagram\n';
  if (rnd() < 0.4) src += `  direction ${pick(['TB', 'LR', 'RL', 'BT', 'lr'])}\n`;
  if (rnd() < 0.06) src += `  accTitle: er acc\n`;
  if (rnd() < 0.04) src += `  accDescr {\n   multi\n     line\n  }\n`;
  const names = Array.from({ length: ri(2, 6) }, () => pick(NAMES)).filter((x, i, xs) => xs.indexOf(x) === i);
  if (rnd() < 0.35) {
    // A typical schema: entities with attributes, joined by relationships.
    for (const n of names) src += `  ${n} ${block(ri(1, 6))}\n`;
    for (let e = ri(1, 6); e > 0; e--) src += '  ' + relation(pick(names), pick(names)) + '\n';
  }
  if (rnd() < 0.3) {
    // Several self-relationships on one entity, across and down.
    const x = pick(names);
    for (let e = ri(1, 4); e > 0; e--) src += '  ' + relation(x, x) + '\n';
    src += '  ' + relation(x, pick(names)) + '\n';
  }
  if (rnd() < 0.3) {
    // Many edges meeting one side of a box.
    const hub = pick(names);
    for (let e = ri(2, 7); e > 0; e--) src += '  ' + relation(hub, pick(['P1', 'P2', 'P3', 'P4', 'P5', 'P6'])) + '\n';
  }
  if (rnd() < 0.35) {
    for (let g = ri(1, 2); g > 0; g--) src += subgraph(0, names).split('\n').map((l) => '  ' + l).join('\n') + '\n';
    for (let e = ri(1, 4); e > 0; e--) src += '  ' + relation(pick(names), pick(names.concat(['G', 'Grp']))) + '\n';
  }
  if (rnd() < 0.3) {
    // Entities with fills of their own: the text, the stripes and the lines follow the fill.
    src += `  PAINT ${block(ri(1, 4))}\n  PAINT2 ${block(ri(1, 3))}\n  PAINT ||--o{ PAINT2 : p\n`;
    src += `  classDef paint ${style()}${rnd() < 0.4 ? ',' + style() : ''}\n  class PAINT${rnd() < 0.5 ? ',PAINT2' : ''} paint\n`;
    if (rnd() < 0.4) src += `  style PAINT2 ${style()}\n`;
    if (rnd() < 0.3) src += `  subgraph FG\n    PAINT\n    PAINT2\n  end\n  style FG ${style()}\n  Z ||--|| PAINT : z\n`;
  }
  const n = ri(0, rnd() < 0.2 ? 18 : 7);
  for (let i = 0; i < n; i++) src += (rnd() < 0.3 ? '  ' : '') + statement(names) + (rnd() < 0.03 ? ';\n' : pick(['\n', '\n', '\n', '\r\n']));
  if (rnd() < 0.03 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '{', '}', '~', ':', '|', '--', '\n', '[', '`', '?', '::']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.2) options.maxWidth = pick([200, 320, 480, 640, 1000]);
  if (rnd() < 0.1) options.fontSize = pick([10, 12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  if (rnd() < 0.2) options.monospace = pick([['attributes'], ['members', 'attributes'], ['messages']]);
  if (rnd() < 0.2) options.idPrefix = pick(['a', 'mermaid-1', 'é日本']);
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  if (Object.keys(options).length) { fs.writeFileSync(optPath, Object.entries(options).map(([k, v]) => `${k}=${Array.isArray(v) ? v.join(',') : v}`).join('\n') + '\n'); withOpts++; }
  for (const ext of ['svg', 'err']) { const f = path.join(dir, `${c}.${ext}`); if (fs.existsSync(f)) fs.unlinkSync(f); }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, options).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', withOpts);
