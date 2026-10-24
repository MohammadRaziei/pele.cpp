// Collects every regular expression literal in Pele's sources: bundles them with esbuild (types
// stripped, modern syntax lowered) and reads the regex tokens with esprima.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import { createRequire } from 'module';
import fs from 'fs';
import path from 'path';
const require = createRequire('/home/claude/pele/package.json');
const esprima = require('esprima');

const files = [];
(function walk(d) { for (const f of fs.readdirSync(d)) { const p = path.join(d, f); if (fs.statSync(p).isDirectory()) walk(p); else if (p.endsWith('.ts') && !p.endsWith('.d.ts')) files.push(p); } })('/home/claude/pele/src');
const found = new Map();
for (const f of files) {
  let code;
  try { code = (await build({ entryPoints: [f], bundle: false, write: false, format: 'esm', target: 'es2017', platform: 'node', logLevel: 'silent', loader: { '.ts': 'ts' } })).outputFiles[0].text; } catch { continue; }
  let tokens;
  try { tokens = esprima.tokenize(code, { tolerant: true }); } catch { continue; }
  for (const t of tokens) if (t.type === 'RegularExpression') {
    const v = t.value; const i = v.lastIndexOf('/');
    const key = v;
    if (!found.has(key)) found.set(key, { source: v.slice(1, i), flags: v.slice(i + 1), file: path.relative('/home/claude/pele/src', f) });
  }
}
const list = [...found.values()];
fs.writeFileSync(process.argv[2] ?? '/tmp/regexes.json', JSON.stringify(list));
console.log('regex literals:', list.length, 'files:', files.length);
