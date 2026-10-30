// Renders random state diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_state_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const dir = process.argv[4] ?? '/tmp/state_cases';
fs.mkdirSync(dir, { recursive: true });
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const NAMES = ['Idle', 'Running', 'Stopped', 'A', 'B', 'C', 'D', 'E', 'Waiting', 'Done', 'S1', 'S2', 'x_1', 'é', 'Failed', 'Ready', 'LongStateNameHere', 'a-b'];
const LABELS = ['start', 'stop', 'go', 'on error', 'a longer transition label that needs to wrap around', 'x<br/>y', '**bold**', 'a & b', '日本', 'tick', 'event(1)', 'ok'];
const DESCR = ['A description', 'two words', 'a much longer description that goes on and on until it wraps', '**bold** text', 'x<br/>y', 'é日本', 'with <b>tag</b>'];
const NOTES = ['a note', 'a longer note text that wraps somewhere in the middle of it', 'x<br/>y', '**b**'];
const CLASSES = ['hot', 'cold', 'big', 'x'];
const COLORS = ['#f9f', '#FFF', '#123', '#ffeeaa', '#003366', 'red', 'navy', 'grey', 'lightgrey', 'gold', 'black', 'white', 'transparent', 'none', 'foo', 'yellow', 'orange', '#000', '#fff', 'RED'];
const STYLES = [() => `fill:${pick(COLORS)}`, () => `fill:${pick(COLORS)},stroke:${pick(COLORS)}`, () => `fill:${pick(COLORS)},color:${pick(COLORS)}`, () => `fill:${pick(COLORS)},stroke:${pick(COLORS)},stroke-width:2px`, 'color:#fff', 'font-weight:bold', 'font-style:italic', 'font-size:20px', 'stroke-dasharray:5 5', 'stroke:#333', 'font-family:serif', 'rx:8'];
const style = () => { const x = pick(STYLES); return typeof x === 'function' ? x() : x; };
const nm = (names) => (names && rnd() < 0.85 ? pick(names) : pick(NAMES));
const ep = (names, own) => (rnd() < 0.12 ? '[*]' : nm(names));
const indent = (s, n = 2) => s.split('\n').map((l) => ' '.repeat(n) + l).join('\n');

function transition(names) {
  const a = ep(names), b = ep(names);
  return `${a} --> ${b}${rnd() < 0.5 ? ' : ' + pick(LABELS) : ''}`;
}

function statement(names, depth) {
  const a = nm(names), b = nm(names);
  switch (ri(0, 21)) {
    case 0: case 1: case 2: case 3: return transition(names);
    case 4: return `${a} --> ${a}${rnd() < 0.6 ? ' : ' + pick(LABELS) : ''}`;
    case 5: return `${a} : ${pick(DESCR)}`;
    case 6: return `state "${pick(DESCR)}" as ${a}`;
    case 7: return `state ${a} <<${pick(['fork', 'join', 'choice'])}>>`;
    case 8: return `note ${pick(['left of', 'right of'])} ${a} : ${pick(NOTES)}`;
    case 9: return `note ${pick(['left of', 'right of'])} ${a}\n  ${pick(NOTES)}\n  more\nend note`;
    case 10: return `${a}:::${pick(CLASSES)}`;
    case 11: return `classDef ${pick(CLASSES)} ${style()}${rnd() < 0.4 ? ',' + style() : ''}`;
    case 12: return `class ${a}${rnd() < 0.3 ? ',' + b : ''} ${pick(CLASSES)}`;
    case 13: return `style ${a} ${style()}${rnd() < 0.3 ? ',' + style() : ''}`;
    case 14: return depth < 2 ? composite(names, depth + 1) : transition(names);
    case 15: return `direction ${pick(['TB', 'LR', 'BT', 'RL'])}`;
    case 16: return rnd() < 0.5 ? `note "${pick(NOTES)}" as N1` : '%% a comment';
    case 17: return `${a}: ${pick(DESCR)}\n${a}: second line`;
    case 18: return rnd() < 0.5 ? `click ${a} href "https://example.com/${a}"` : `click ${a} "https://example.com/${a}" "a tooltip"`;
    case 19: return `${a} --> ${b} : ${pick(LABELS)}\n${a} --> ${b} : ${pick(LABELS)}`;
    case 20: return `state "${pick(DESCR)}" as ${a} {\n  ${transition(names)}\n}`;
    default: return a;
  }
}

const seenComposites = [];
function composite(names, depth) {
  const id = pick(['Active', 'Group', 'Machine', 'Sub', 'Inner', 'Outer']);
  seenComposites.push(id);
  let s = `state ${id} {\n`;
  if (rnd() < 0.3) s += `  direction ${pick(['TB', 'LR', 'BT', 'RL'])}\n`;
  const inner = [id + '_a', id + '_b', id + '_c'];
  for (let i = ri(1, 4); i > 0; i--) s += indent(rnd() < 0.7 ? transition(inner.concat(['[*]'])) : statement(inner, depth)) + '\n';
  if (rnd() < 0.3) { s += '  --\n'; for (let i = ri(1, 3); i > 0; i--) s += indent(transition(inner.concat(['[*]']))) + '\n'; }
  return s + '}';
}

const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(['State title', 'x', 'A <b>b</b>', 'A long diagram title that is wide'])}\n---\n`;
  const cfg = {};
  if (rnd() < 0.1) cfg.state = { nodeSpacing: pick([20, 80]), rankSpacing: pick([20, 100]), wrappingWidth: pick([100, 300]) };
  if (rnd() < 0.15) cfg.flowchart = { curve: pick(['linear', 'step', 'stepBefore', 'stepAfter', 'basis', 'cardinal', 'monotoneX']) };
  if (Object.keys(cfg).length) src += `%%{init: ${JSON.stringify(cfg)}}%%\n`;
  src += pick(['stateDiagram-v2', 'stateDiagram-v2', 'stateDiagram']) + '\n';
  if (rnd() < 0.3) src += `  direction ${pick(['TB', 'LR', 'RL', 'BT'])}\n`;
  if (rnd() < 0.05) src += `  accTitle: state acc\n`;
  if (rnd() < 0.04) src += `  accDescr {\n   multi\n     line\n  }\n`;
  if (rnd() < 0.03) src += `  hide empty description\n  scale 350 width\n`;
  const names = Array.from({ length: ri(2, 6) }, () => pick(NAMES)).filter((x, i, xs) => xs.indexOf(x) === i);
  if (rnd() < 0.5) {
    // A chain from the start state through the names to the end.
    src += `  [*] --> ${names[0]}\n`;
    for (let i = 0; i + 1 < names.length; i++) src += `  ${names[i]} --> ${names[i + 1]}${rnd() < 0.5 ? ' : ' + pick(LABELS) : ''}\n`;
    if (rnd() < 0.7) src += `  ${names[names.length - 1]} --> [*]\n`;
  }
  if (rnd() < 0.3) {
    // Fork and join around parallel states.
    src += `  state fk <<fork>>\n  state jn <<join>>\n  ${names[0]} --> fk\n`;
    for (let i = ri(2, 4); i > 0; i--) { const t = pick(NAMES); src += `  fk --> ${t}\n  ${t} --> jn\n`; }
    src += `  jn --> ${pick(names)}\n`;
    if (rnd() < 0.5) src += `  state ch <<choice>>\n  ${pick(names)} --> ch\n  ch --> ${pick(names)} : yes\n  ch --> ${pick(names)} : no\n`;
  }
  seenComposites.length = 0;
  if (rnd() < 0.35) for (let g = ri(1, 2); g > 0; g--) src += indent(composite(names, 0)) + '\n';
  if (seenComposites.length && rnd() < 0.5) {
    // Fills on composites, with transitions that cross their borders and run on top of them.
    for (const id of seenComposites) {
      if (rnd() < 0.7) src += `  style ${id} fill:${pick(COLORS)}${rnd() < 0.3 ? ',stroke:' + pick(COLORS) : ''}\n`;
      if (rnd() < 0.6) src += `  ${pick(names)} --> ${id}_a : in\n  ${id}_b --> ${pick(names)} : out\n  ${id}_a --> ${id}_c\n`;
    }
  }
  if (rnd() < 0.2) src += `  ${names[0]} --> Active\n  Active --> ${names[names.length - 1]}\n  Active --> Active : again\n`;
  if (rnd() < 0.25) {
    const x = pick(names);
    for (let e = ri(1, 4); e > 0; e--) src += `  ${x} --> ${x} : ${pick(LABELS)}\n`;
  }
  if (rnd() < 0.25) src += `  classDef paint ${style()}${rnd() < 0.4 ? ',' + style() : ''}\n  class ${pick(names)} paint\n  style ${pick(names)} ${style()}\n`;
  const n = ri(0, rnd() < 0.2 ? 16 : 7);
  for (let i = 0; i < n; i++) src += indent(statement(names, 0)) + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.03 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '{', '}', '<<', ':', '--', '\n', '[*', ':::', '%%', 'state ']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.2) options.maxWidth = pick([200, 320, 480, 640, 1000]);
  if (rnd() < 0.1) options.fontSize = pick([10, 12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
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
