// Renders random kanban diagrams with pele.js and writes the sources and the SVG it gives, for
// tests/test_e2e.cpp: <dir>/N.mmd, N.svg (or N.err) and, when options were given, N.opts.
//   node tools/gen_kanban_cases.mjs [seed] [count] [dir]
import fs from 'fs';
import path from 'path';
import { render } from '/home/claude/pele/dist/pele.js';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];

// Icons the host has: a name with "book" or "home" (the same rule as tests/test_e2e.cpp).
const icons = (name) => (/book|home/.test(name) ? '<path d="M3 3h18v18H3z"/>' : undefined);

const WORDS = ['Todo', 'In progress', 'Done', 'Review', 'Backlog', 'Ship it', 'A much longer column title that has to wrap around', 'ReallyLongUnbrokenWordThatCannotFitInACard', '日本語のタスク', 'é ü ñ', 'Fix <b>bold</b> &amp; more', 'fa:fa-book Read docs', 'x'.repeat(50), 'T', 'Two words', "it's", '😀 emoji card'];
const MD = ['`**bold** and *italic*`', '`plain markdown`', '`a long markdown text that goes on and on for a while`'];
const PEOPLE = ['Kim', 'Alex Johnson', 'A very long assignee name here', '日本', 'x'];
const TICKETS = ['MC-1', 'ABC-1234', 'T', 'a&b', 'x<y', '$&', 'MC-2 extra'];
const PRIOS = ['Very High', 'High', 'Medium', 'Low', 'Very Low', 'high', ' LOW ', 'urgent', ''];
const ICONS = ['fa fa-book', 'mdi mdi-home', 'fa fa-star', 'book', 'nothing'];
const CLASSES = ['important', 'a b', 'x<y', '"q"', 'urgent big'];

function node(id, indent) {
  const label = rnd() < 0.15 ? `["${pick(MD)}"]` : rnd() < 0.2 ? `["${pick(WORDS).replace(/"/g, '')}"]` : `[${pick(WORDS).replace(/[\]\[()"]/g, '')}]`;
  let line = ' '.repeat(indent) + id + (rnd() < 0.9 ? label : '');
  if (rnd() < 0.55) {
    const f = [];
    if (rnd() < 0.6) f.push(`ticket: ${pick(TICKETS)}`);
    if (rnd() < 0.5) f.push(`assigned: '${pick(PEOPLE)}'`);
    if (rnd() < 0.5) f.push(`priority: '${pick(PRIOS)}'`);
    if (rnd() < 0.25) f.push(`icon: '${pick(ICONS)}'`);
    if (rnd() < 0.1) f.push(`label: '${pick(WORDS).replace(/'/g, '')}'`);
    if (rnd() < 0.1) f.push(`shape: ${rnd() < 0.85 ? pick(['rect', 'circle', 'rounded']) : pick(['Rect', 'a_b', 'İ', 'ΑΣ', 'K'])}`);
    if (rnd() < 0.03) f.push(pick(['ticket: [', 'a: b: c', '"unterminated', '- x', 'ticket: 0', 'ticket: 12', 'assigned: true', 'priority: 5']));
    if (f.length) line += rnd() < 0.2 ? `@{\n${' '.repeat(indent + 2)}${f.join('\n' + ' '.repeat(indent + 2))}\n${' '.repeat(indent)}}` : `@{ ${f.join(', ')} }`;
  }
  if (rnd() < 0.15) line += '\n' + ' '.repeat(indent) + `::icon(${pick(ICONS)})`;
  if (rnd() < 0.12) line += '\n' + ' '.repeat(indent) + `:::${pick(CLASSES)}`;
  return line;
}

function body() {
  const lines = [];
  const col = ri(0, 3) * 2 + 2;
  const sections = ri(0, 5);
  for (let s = 0; s < sections; s++) {
    lines.push(node('col' + s, col));
    for (let i = ri(0, 4); i > 0; i--) lines.push(node('t' + s + '_' + i, col + ri(1, 3) * 2));
    if (rnd() < 0.05) lines.push(' '.repeat(ri(0, 6)) + pick(['%% a comment', '', '   ', '@{ x: 1 }', '::icon(book)', ':::cls']));
  }
  if (rnd() < 0.05) lines.push(node('misplaced', Math.max(0, col - 2)));
  if (rnd() < 0.03) lines.push(pick(['x{{y}}', 'a))b((', 'q)', '[', '(unclosed', 'n((c))', '"str"', '`md`', 'a@b']));
  return lines;
}

const dir = process.argv[4] ?? '/tmp/e2e_kanban';
fs.mkdirSync(dir, { recursive: true });
const count = Number(process.argv[3] ?? 200);
let ok = 0, err = 0, withOpts = 0;
for (let c = 0; c < count; c++) {
  let src = '';
  if (rnd() < 0.12) src += `---\ntitle: ${pick(WORDS).replace(/[:#]/g, '')}\n---\n`;
  if (rnd() < 0.55) {
    const cfg = {};
    if (rnd() < 0.5) cfg.sectionWidth = pick([100, 150, 200, 300, 50, 5000, 0, -1, 'x']);
    if (rnd() < 0.7) cfg.ticketBaseUrl = pick(['https://example.com/browse/#TICKET#', 'https://x.org/#TICKET#/#TICKET#', 'javascript:alert(1)', '/rel/#TICKET#', 'https://x.org/none', '']);
    if (rnd() < 0.1) cfg.useMaxWidth = false;
    src += `%%{init: {"kanban": ${JSON.stringify(cfg)}}}%%\n`;
  }
  src += (rnd() < 0.9 ? pick(['kanban', 'kanban', 'kanban', 'kanban  ', '\n\nkanban', '%% c\nkanban']) : pick(['Kanban', 'KANBAN', '  kanban'])) + '\n';
  for (const line of body()) src += line + pick(['\n', '\n', '\n', '\r\n']);
  if (rnd() < 0.03 && src.length > 3) { const at = ri(0, src.length - 1); src = src.slice(0, at) + pick(['"', '[', '{', '}', '@', ':', '(', ')', '\n', '%']) + src.slice(at); }
  const options = {};
  if (rnd() < 0.4) options.maxWidth = pick([200, 320, 400, 500, 640, 800, 1200, 60]);
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
