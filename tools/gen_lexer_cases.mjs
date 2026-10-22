// Runs Pele's flowchart tokenizer on random sources.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { tokenize } from './diagrams/flowchart/lexer.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/lexer_bundle.mjs', out.outputFiles[0].text);
const L = await import('/tmp/lexer_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const HEAD = ['graph TD', 'graph LR\n', 'flowchart TB;', 'flowchart RL\n', 'graph BT', 'graph', 'flowchart', 'flowchart-elk TD\n', 'graph <\n', 'graph >\n', 'graph ^\n', 'graph v\n', 'swimlane-beta LR\n', '\n\ngraph TD\n', 'graph TD;\n', 'graph\n', 'flowchart TD   \n'];
const IDS = ['A', 'B', 'C', 'node1', 'x', 'o', 'xyz', 'ox', 'a-b', 'a.b', 'A1', '1', '42', 'v', 'vv', 'é', 'ü', '日本', 'end', 'endx', 'default', 'style1', 'classy', 'A_B', 'a/b', 'a\\b', 'a*b', 'a&b', "it's", 'a?b', 'a`b', 'a#b', 'a%b', 'a!b'];
const LINKS = ['-->', '---', '--', '-.->', '-.-', '==>', '===', '~~~', '--x', '--o', 'x--x', 'o--o', '<-->', '<--', '--->', '----', '-..->', '-...-', '==>>', '====>', '~~~~', ' --> ', ' --- ', '-- text -->', '-- text ---', '-. text .->', '== text ==>', '--|text|-->', '---|t|', '--\"quoted\"-->', 'e1@-->', 'e2@---', 'x-->', 'o-->', '<-.->', '<==>', '-', '--', '->', '- ->', '.-', '-.', '=', '==', '===>>'];
const SHAPES = ['[text]', '(text)', '((text))', '(((text)))', '{text}', '{{text}}', '[[text]]', '[(text)]', '([text])', '>text]', '[/text/]', '[\\text\\]', '[/text\\]', '[\\text/]', '(-text-)', '["quoted"]', '["`markdown`"]', '[text with (parens)]', '[a|b]', '[|field:value|text]', '[]', '()', '{}', '[text', '(text', '[/text', '[\\ta]', '((text)', '[<b>x</b>]', '[a&b]', '[a;b]', '[a #35; b]', '(\"a b\")', '(a\nb)', '[\"a\nb\"]', '[a~~~b]', '[a --> b]'];
const STATE = ['subgraph one', 'subgraph two [Two Title]', 'subgraph', 'end', 'direction TB', 'direction LR', 'direction RL', 'direction BT', 'direction TD', '  direction  LR  ', 'direction XX', 'style A fill:#f9f,stroke:#333,stroke-width:4px', 'classDef c1 fill:#fff', 'class A,B c1', 'linkStyle 0 stroke:red', 'linkStyle default interpolate basis', 'linkStyle 1,2 stroke-width:2px', 'click A callback', 'click A call cb()', 'click A call cb(1,2)', 'click A href "http://x.y"', 'click A "http://x.y" _blank', 'click A href "u" "tip" _self', 'click A "u" "tip"', 'accTitle: My title', 'accDescr: My desc', 'accDescr {\n multi\n line\n}', 'A@{ shape: rect, label: "x" }', 'A@{\n  shape: cyl\n  label: "multi\n line"\n}', 'A:::cls', 'A:::cls & B:::cls2', 'A & B', 'A  &  B', 'A&B', '%% comment', '', '   ', '\t', 'click', 'call x', 'href x', '_self', '_blank', '_parent', '_top', ';', ';;', ',', ':', '::', ':::', '*', '#', '@', '@{', '@x', 'a@b', 'a@@b', '"str"', '"`md`"', '"unterminated', '`', '^', '|', '||', '|x|', '}', '{', ')', ']', '<', '>', '<>', '~', '~~', '~~~', '$', '\u00e9\u00e8', '\u4e2d\u6587', '\u2028', '\u00a0', '😀'];

function line() {
  const k = ri(0, 9);
  const a = pick(IDS), b = pick(IDS);
  if (k <= 3) return `${a}${pick(['', '', ' '])}${pick(SHAPES)}${pick(['', ' '])}${pick(LINKS)}${pick(['', ' ', ' '])}${b}${pick(['', pick(SHAPES)])}`;
  if (k === 4) return `${a} ${pick(LINKS)} ${b} ${pick(LINKS)} ${pick(IDS)}`;
  if (k === 5) return pick(STATE);
  if (k === 6) return `${a}${pick(SHAPES)}`;
  if (k === 7) return `${a} ${pick(LINKS)} ${b}${pick([';', '', ' ;'])}`;
  if (k === 8) return pick(STATE) + ' ' + pick(IDS);
  return pick(STATE);
}

const lines = [];
const count = Number(process.argv[3] ?? 600);
for (let c = 0; c < count; c++) {
  let src = pick(HEAD) + (rnd() < 0.5 ? '\n' : ' ');
  for (let i = ri(0, 6); i > 0; i--) src += line() + pick(['\n', '\n', '\n  ', ';', '\r\n']);
  if (rnd() < 0.2 && src.length > 3) { const at = ri(0, src.length - 1); src = rnd() < 0.5 ? src.slice(0, at) + src.slice(at + 1) : src.slice(0, at) + pick(['"', '[', '(', '{', '|', '-', '>', '@', ' ', '\n', '`', '\\', '~', '=']) + src.slice(at); }
  if (rnd() < 0.05) src = pick(['', 'x', '--', '@', 'accTitle:', 'graph TD\naccTitle', 'graph TD\nA-->']);
  const t = L.tokenize(src);
  lines.push(`lex ${hex16(src)} ${t.types.length}`);
  for (let i = 0; i < t.types.length; i++) lines.push(`${t.types[i]} ${t.starts[i]} ${hex16(t.texts[i])}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/lexer_cases.txt', lines.join('\n') + '\n');
console.log('cases', count, 'lines', lines.length);
