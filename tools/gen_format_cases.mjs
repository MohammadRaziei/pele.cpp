// Runs toFixed, toExponential and toPrecision on random numbers (and on numbers exactly halfway
// between two roundings), for tests/test_format.cpp.   node tools/gen_format_cases.mjs [seed] [count] [out]
import fs from 'fs';

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const bits = (v) => { const b = Buffer.alloc(8); b.writeDoubleBE(v); return b.toString('hex'); };

function value() {
  const k = ri(0, 9);
  if (k === 0) return pick([0, 1, 5, 10, 100, 999.5, 0.5, 1.5, 2.5, 0.05, 0.005, 1e21, 1e21 - 1e5, 123456789012345680000, 1e-7, 5e-324, 1.7976931348623157e308, 0.1, 0.30000000000000004, 4.35, 1.005, 8.345, 10.235, 2 ** 53]);
  if (k === 1) return ri(0, 100000) / pick([2, 4, 8, 16, 32, 64, 1024]);          // exact ties
  if (k === 2) return ri(0, 1000) / pick([10, 100, 1000]);
  if (k === 3) return Math.round(rnd() * 1e6) * 10 ** ri(0, 22);
  if (k === 4) return rnd() * 10 ** ri(-12, 6);
  if (k === 5) return rnd();
  if (k === 6) return ri(1, 99) / 100 + ri(0, 9) * 10 ** ri(0, 3);
  if (k === 7) return 9.5 * 10 ** ri(-8, 22) / pick([1, 10]);                      // carries into a new digit
  if (k === 8) return Number('9.' + '9'.repeat(ri(1, 16)) + 'e' + ri(-10, 22));
  return rnd() * 10 ** ri(-3, 25);
}
const lines = [];
const count = Number(process.argv[3] ?? 6000);
for (let c = 0; c < count; c++) {
  const v = value();
  const f = ri(0, 20), p = ri(1, 21);
  lines.push(`n ${bits(v)} ${f} ${p} ${v.toFixed(f)} ${v.toExponential(f)} ${v.toPrecision(p)}`);
}
// parseFloat on text made of what a number token can hold, and a few things it cannot.
const hex16 = (t) => 'x' + Array.from({ length: t.length }, (_, i) => t.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const PARTS = ['0', '1', '5', '9', '12', '007', '.', '.', '_', ',', '..', 'e', 'E', '+', '-', '3', ' ', '\u00a0', 'Infinity', 'e5', 'e-3', '0x1', '1e', '9'.repeat(25), '1_000', '\n'];
for (let c = 0; c < count; c++) {
  let t = '';
  for (let n = ri(0, 6); n > 0; n--) t += pick(PARTS);
  lines.push(`p ${hex16(t)} ${Object.is(parseFloat(t), -0) ? '-0' : String(parseFloat(t))}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/format_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
