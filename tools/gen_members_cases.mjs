// Runs parseGenericTypes (src/diagrams/common/generics.ts) and ClassMember (src/diagrams/class/members.ts)
// on random text, for tests/test_members.cpp.   node tools/gen_members_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { parseGenericTypes } from './diagrams/common/generics.ts'; export { ClassMember } from './diagrams/class/members.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/members_bundle.mjs', out.outputFiles[0].text);
const { parseGenericTypes, ClassMember } = await import('/tmp/members_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

const GEN = ['List', 'Map', 'int', 'string', 'T', '~', '~', '~', ',', ',', ' ', 'a', 'K', 'V', '(', ')', '<', '>', 'é', '日', '😀', '\n'];
const MEM = ['+', '-', '#', '~', '$', '*', '(', ')', '(', ')', ' ', ' ', '\t', '\n', '\r\n', ':', ',', 'run', 'getName', 'int', 'String', 'List~int~', 'Map~K,V~', 'x', '_', 'name', '\u00a0', '\u2028', 'é', '日', '😀', '()', '(int a, int b)', '*abstract*', '$static$'];
const lines = [];
const count = Number(process.argv[3] ?? 4000);
for (let c = 0; c < count; c++) {
  let s = '';
  for (let n = ri(0, 10); n > 0; n--) s += pick(GEN);
  lines.push(`g ${hex16(s)} ${hex16(parseGenericTypes(s))}`);
}
for (let c = 0; c < count; c++) {
  let s = rnd() < 0.5 ? pick(['+', '-', '#', '~', '']) : '';
  for (let n = ri(0, 9); n > 0; n--) s += pick(MEM);
  for (const type of ['method', 'attribute']) {
    const m = new ClassMember(s, type);
    const d = m.getDisplayDetails();
    lines.push(`m ${type} ${hex16(s)} ${m.memberType} ${hex16(m.visibility)} ${hex16(m.classifier)} ${hex16(m.id)} ${hex16(m.parameters)} ${hex16(m.returnType)} ${hex16(d.displayText)} ${hex16(d.cssStyle)}`);
  }
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/members_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
