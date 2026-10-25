// Renders random mindmaps with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_mindmap_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

// Icons the host has: a name with "book" or "home" (the same rule as tests/test_e2e.cpp).
const icons = (name) => (/book|home/.test(name) ? '<path d="M3 3h18v18H3z"/>' : undefined);

const WORDS = ['Origins', 'Long history', 'Popularisation', 'Research', 'On effectiveness<br/>and features', 'Tools', 'A much longer node text that has to wrap onto several lines to fit', 'ReallyLongUnbrokenWordWithoutAnySpacesAtAll', '日本語', 'é ü ñ', 'Fix <b>bold</b> &amp; more', 'fa:fa-book Read', 'T', 'Two words', "it's", '😀 emoji', 'x'.repeat(40), 'Automatic creation', 'Mind map'];
const MD = ['`**bold** and *italic*`', '`plain markdown`', '`a longer markdown text that goes on for a while`'];
const ICONS = ['fa fa-book', 'mdi mdi-home', 'fa fa-star', 'book', 'nothing'];
const CLASSES = ['important', 'a b', 'x<y', 'urgent big'];
const clean = (s) => s.replace(/[\]\[(){}"]/g, '');

function node(indent) {
  const id = pick(['', '', 'id' + ri(1, 99), 'n' + ri(1, 9)]);
  const text = pick(WORDS);
  const form = ri(0, 9);
  let line;
  if (form < 4) line = id + clean(text) || 'x';
  else if (form === 4) line = `${id}[${clean(text)}]`;
  else if (form === 5) line = `${id}(${clean(text)})`;
  else if (form === 6) line = `${id}((${clean(text)}))`;
  else if (form === 7) line = `${id}))${clean(text)}((`;
  else if (form === 8) line = `${id}{{${clean(text)}}}`;
  else line = `${id})${clean(text)}(`;
  if (rnd() < 0.12) line = `${id}["${pick(MD)}"]`;
  else if (rnd() < 0.1) line = `${id}["${clean(text)}"]`;
  let out = ' '.repeat(indent) + line;
  if (rnd() < 0.18) out += '\n' + ' '.repeat(indent + 1) + `::icon(${pick(ICONS)})`;
  if (rnd() < 0.12) out += '\n' + ' '.repeat(indent + 1) + `:::${pick(CLASSES)}`;
  return out;
}

function tree(indent, depth, lines) {
  const kids = depth === 0 ? (rnd() < 0.3 ? ri(8, 15) : ri(0, 6)) : ri(0, depth < 3 ? 3 : 1);
  for (let i = 0; i < kids; i++) {
    lines.push(node(indent));
    if (depth < 4 && rnd() < 0.55) tree(indent + ri(1, 3) * 2, depth + 1, lines);
  }
}

function body() {
  const lines = [];
  const base = ri(0, 2) * 2 + 2;
  lines.push(node(base));
  tree(base + 2, 0, lines);
  if (rnd() < 0.04) lines.push(node(base));
  if (rnd() < 0.04) lines.push(' '.repeat(ri(0, 8)) + pick(['%% a comment', '', '   ', '::icon(book)', ':::cls']));
  if (rnd() < 0.03) lines.push(pick(['x{{y}}', 'a))b((', 'q)', '[', '(unclosed', 'n((c))', '"str"', '`md`', 'a@b']));
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_mindmap';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(WORDS).replace(/[:#<&]/g, '')}\n---\n`;
  if (rnd() < 0.3) src += `%%{init: {"mindmap": ${JSON.stringify({ maxNodeWidth: pick([60, 100, 150, 300, 5000, 0, -1, 'x']) })}}}%%\n`;
  src += (rnd() < 0.92 ? pick(['mindmap', 'mindmap', 'mindmap', 'mindmap  ', '\n\nmindmap', '%% c\nmindmap']) : pick(['Mindmap', 'MINDMAP', '  mindmap'])) + '\n';
  for (const line of body()) src += line + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.03 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '[', '{', '}', '@', ':', '(', ')', '\n', '%']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.45) options.maxWidth = pick([200, 320, 400, 500, 640, 800, 1200, 60]);
  if (rnd() < 0.1) options.fontSize = pick([12, 14, 18]);
  if (rnd() < 0.1) options.padding = pick([0, 4, 12]);
  const withIcons = rnd() < 0.5;
  fs.writeFileSync(path.join(dir, `${c}.mmd`), src);
  const optPath = path.join(dir, `${c}.opts`);
  if (fs.existsSync(optPath)) fs.unlinkSync(optPath);
  const lines = Object.entries(options).map(([k, v]) => `${k}=${v}`);
  if (withIcons) lines.push('icons=1');
  if (lines.length) { fs.writeFileSync(optPath, lines.join('\n') + '\n'); withOpts++; }
  try { fs.writeFileSync(path.join(dir, `${c}.svg`), render(src, { ...options, ...(withIcons ? { icons } : {}) }).svg); ok++; }
  catch (e) { fs.writeFileSync(path.join(dir, `${c}.err`), `${e.code}\n${e.message}`); err++; }
}
console.log('rendered', ok, 'errors', err, 'with options', withOpts);
