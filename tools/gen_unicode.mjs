// Regenerates src/unicode_data.inc: the ranges that JavaScript's /[\p{L}\p{N}]/u matches.
const re = /[\p{L}\p{N}]/u;
const out = [];
let start = -1;
for (let cp = 0; cp <= 0x10ffff; cp++) {
  const ok = cp >= 0xd800 && cp <= 0xdfff ? false : re.test(String.fromCodePoint(cp));
  if (ok && start < 0) start = cp;
  if (!ok && start >= 0) { out.push([start, cp - 1]); start = -1; }
}
if (start >= 0) out.push([start, 0x10ffff]);
console.log('inline constexpr unsigned kLetterOrNumber[][2] = {');
console.log(out.map(([a, b]) => `    {${a}, ${b}},`).join('\n'));
console.log('};');
