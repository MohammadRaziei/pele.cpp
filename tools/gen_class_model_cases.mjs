// Parses random class diagrams with Pele's ClassDb (src/diagrams/class/{parser,db}.ts) and writes the
// model as text, or the error message, for tests/test_class_model.cpp.
//   node tools/gen_class_model_cases.mjs [seed] [count] [out]
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({
  stdin: { contents: "export { ClassDb } from './diagrams/class/db.ts'; export { parseClassDiagram } from './diagrams/class/parser.ts';", resolveDir: '/home/claude/pele/src', loader: 'ts' },
  bundle: true, format: 'esm', write: false, platform: 'node',
});
fs.writeFileSync('/tmp/classmodel_bundle.mjs', out.outputFiles[0].text);
const { ClassDb, parseClassDiagram } = await import('/tmp/classmodel_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');

// The same text is made by the C++ test: a string as length:text, nothing as -, a list as count[items].
const f = (s) => (s === undefined ? '-' : `${s.length}:${s}`);
const list = (a) => `${a.length}[${a.map(f).join('')}]`;
const member = (m) => `(${f(m.memberType)}${f(m.visibility)}${f(m.classifier)}${f(m.id)}${f(m.parameters)}${f(m.returnType)})`;
function dump(db) {
  let s = `D${f(db.direction)}${f(db.title)}${f(db.accTitle)}${f(db.accDescr)}`;
  for (const c of db.classes.values())
    s += `C${f(c.id)}${f(c.type)}${f(c.label)}${f(c.cssClasses)}${list(c.annotations)}${list(c.styles)}${f(c.parent)}${f(c.link)}${f(c.linkTarget)}${c.haveCallback ? 1 : 0}${f(c.tooltip)}${c.members.length}${c.members.map(member).join('')}${c.methods.length}${c.methods.map(member).join('')};`;
  for (const r of db.relations) s += `R${f(r.id1)}${f(r.id2)}${f(r.relationTitle1)}${f(r.relationTitle2)}${f(r.title)}${r.relation.type1},${r.relation.type2},${r.relation.lineType};`;
  for (const n of db.notes.values()) s += `N${f(n.id)}${f(n.class)}${f(n.text)}${n.index}${f(n.parent)};`;
  for (const n of db.namespaces.values()) s += `S${f(n.id)}${f(n.label)}${list([...n.classes.keys()])}${list([...n.notes.keys()])}${list([...n.children.keys()])}${f(n.parent)}${n.explicit ? 1 : 0};`;
  for (const i of db.interfaces) s += `I${f(i.id)}${f(i.label)}${f(i.classId)};`;
  for (const y of db.styleClasses.values()) s += `Y${f(y.id)}${list(y.styles)}${list(y.textStyles)};`;
  return s;
}

const NAMES = ['Animal', 'Dog', 'Duck', 'Fish', 'Zebra', 'Shape', 'A', 'B', 'C', 'Order', 'Customer', 'Item', 'Node', 'List~T~', 'Map~K,V~', '`back tick`', 'é', '日本', 'x_1', '9lives', 'a.b'];
const MEMBERS = ['+int age', '-String name', '#double w', '~bool ok', '+swim()', '-run(int a) int', '+abstractM()*', '+staticM()$', 'List~int~ items', '+get(a, b) List~int~', 'plain', '$', '<<interface>>', '<<enum>>', 'x()', '+String id', 'run)', ')'];
const REL = ['<|--', '--|>', '*--', '--*', 'o--', '--o', '<--', '-->', '..>', '<..', '..|>', '<|..', '--', '..', '()--', '--()', '<|-- ', '*..', '..o'];
const STYLES = ['fill:#f9f', 'stroke:#333,stroke-width:2px', 'color:#fff', 'font-style:italic', 'fill:red;stroke:blue'];
function statement() {
  const a = pick(NAMES), b = pick(NAMES);
  switch (ri(0, 18)) {
    case 0: return `class ${a}`;
    case 1: return `class ${a} {\n${Array.from({ length: ri(0, 4) }, () => '  ' + pick(MEMBERS)).join('\n')}\n}`;
    case 2: return `${a} ${pick(REL)} ${b}`;
    case 3: return `${a} "${pick(['1', '*', '0..1', 'many'])}" ${pick(REL)} "${pick(['1', '*', 'n'])}" ${b} : ${pick(['uses', 'owns', 'has a', ''])}`;
    case 4: return `${a} : ${pick(MEMBERS)}`;
    case 5: return `<<${pick(['interface', 'service', 'abstract'])}>> ${a}`;
    case 6: return `class ${a} <<${pick(['interface', 'enum'])}>>`;
    case 7: return `note "${pick(['a note', 'two\\nlines', 'x'])}"`;
    case 8: return `note for ${a} "${pick(['about it', 'fix', 'x'])}"`;
    case 9: return `classDef ${pick(['hot', 'cold', 'big'])} ${pick(STYLES)}${rnd() < 0.4 ? ',' + pick(STYLES) : ''}`;
    case 10: return `class ${a}:::${pick(['hot', 'cold'])}`;
    case 11: return `cssClass "${a},${b}" ${pick(['hot', 'cold'])}`;
    case 12: return `style ${a} ${pick(STYLES)}`;
    case 13: return `namespace ${pick(['Shapes', 'Core', 'A.B', 'N'])} {\n  class ${a}\n  class ${b}\n${rnd() < 0.3 ? '  note "in ns"\n' : ''}}`;
    case 14: return `link ${a} "https://example.com/${pick(['x', 'a&amp;b', ''])}" "tip"${rnd() < 0.5 ? ' _blank' : ''}`;
    case 15: return `click ${a} href "${pick(['https://x.org', 'javascript:alert(1)', '/rel'])}" "tip"${rnd() < 0.3 ? ' _top' : ''}`;
    case 16: return `callback ${a} "doIt" "tip"`;
    case 17: return `click ${a} call fn(${pick(['', 'x, y'])})`;
    default: return pick([`direction ${pick(['TB', 'LR', 'RL', 'BT'])}`, `class ${a}["Label ${b}"]`, `${a}~T~ <|-- ${b}`, `%% comment`, `accTitle: t`, `accDescr: d`, `accDescr {\n  multi\n}`, `${a} : ${pick(MEMBERS)}\n${a} : ${pick(MEMBERS)}`]);
  }
}
const lines = [];
const count = Number(process.argv[3] ?? 2000);
for (let c = 0; c < count; c++) {
  let s = pick(['classDiagram', 'classDiagram', 'classDiagram-v2', '']) + '\n';
  for (let n = ri(0, 8); n > 0; n--) s += (rnd() < 0.3 ? '  ' : '') + statement() + '\n';
  if (rnd() < 0.05) s = s.slice(0, ri(0, s.length)) + pick(['"', '{', '}', '~', ':', '<<', '--', '\n', '[', ']', '(']) + s.slice(ri(0, s.length));
  const db = new ClassDb();
  let result;
  try { parseClassDiagram(s, db); result = 'ok ' + hex16(dump(db)); } catch (e) { result = 'err ' + hex16(e.message); }
  lines.push(`p ${hex16(s)} ${result}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/class_model_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
