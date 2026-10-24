// Renders random `info` diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp. Each case is one file pair: <dir>/N.mmd and <dir>/N.svg (or N.err).
//   node tools/gen_info_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

const TITLES = ['Hello', 'My Chart', 'A much longer title for the info diagram that goes on', '日本語のタイトル', 'x  y   z', '<b>bold</b> & more', 'a%%b', 'é ü ñ', '  spaced  ', '', 'T'];
const LINES = [
  () => `title ${pick(TITLES)}`, () => `title: ${pick(TITLES)}`, () => `title${pick(['', ' ', '\t'])}`, () => `accTitle: ${pick(TITLES)}`, () => `accTitle:${pick(TITLES)}`,
  () => `accDescr: ${pick(TITLES)}`, () => `accDescr {\n${pick(['', '  ', '\t'])}${pick(TITLES)}\n${pick(['  ', ''])}${pick(TITLES)}\n}`, () => `accDescr{${pick(TITLES)}}`,
  () => '%% a comment', () => `title ${pick(TITLES)} %% trailing`, () => `%%{init: {"theme": "dark"}}%%`, () => 'showInfo', () => 'info', () => 'unknown', () => 'title', () => '   ',
];
const dir = process.argv[4] ?? '/tmp/e2e_info';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.2) src += `---\ntitle: ${pick(TITLES) || 'F'}\n${rnd() < 0.3 ? 'config:\n  theme: dark\n' : ''}---\n`;
  if (rnd() < 0.15) src += pick(['%%{init: {"info": {"useMaxWidth": false}}}%%\n', '%% c\n', '\n\n', '  \n']);
  src += pick(['info', 'info', 'info', 'info\n', 'info  ', 'info showInfo', 'info\nshowInfo', 'info\n\nshowInfo\n']);
  for (let i = ri(0, 4); i > 0; i--) src += pick(['\n', '\n', '\r\n', '\n  ', ';']) + pick(LINES)();
  if (rnd() < 0.2) src += pick(['\n', '\n\n', '']);
  if (rnd() < 0.08 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '[', '{', '}', '%', ':', '\\', '#', ';', ' ']) + src.slice(at); }
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err);
