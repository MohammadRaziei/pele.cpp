// Runs Pele's util/url.ts on random URLs.
import { build } from '/home/claude/pele/node_modules/esbuild/lib/main.js';
import fs from 'fs';

const out = await build({ entryPoints: ['/home/claude/pele/src/util/url.ts'], bundle: true, format: 'esm', write: false, platform: 'node' });
fs.writeFileSync('/tmp/url_bundle.mjs', out.outputFiles[0].text);
const U = await import('/tmp/url_bundle.mjs');

let seed = Number(process.argv[2] ?? 1);
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296);
const ri = (a, b) => a + Math.floor(rnd() * (b - a + 1));
const pick = (xs) => xs[ri(0, xs.length - 1)];
const hex16 = (s) => 'x' + Array.from({ length: s.length }, (_, i) => s.charCodeAt(i).toString(16).padStart(4, '0')).join('');
const hex8 = (s) => 'x' + Buffer.from(s, 'utf8').toString('hex');

const URLS = [
  'http://example.com', 'https://example.com/a/b?c=d#e', 'HTTP://EXAMPLE.COM/Path', 'mailto:me@x.y', 'tel:+123', 'ftp://x.y/z', 'file:///etc/passwd', '//evil.com/x', '\\\\evil\\x', '/relative/path', './rel', '../up', 'page.html', '#frag',
  'javascript:alert(1)', 'JaVaScRiPt:alert(1)', ' javascript:alert(1)', 'java\tscript:alert(1)', 'java&#9;script:x', '&#106avascript:x', '&#x6a;avascript:x', 'jav&Tab;ascript:x', 'jav&NewLine;ascript:x', 'data:text/html,<x>', 'vbscript:x', '%6aavascript:alert(1)', '%256aavascript:x', 'javascript%3Aalert(1)', 'javascript&colon;alert(1)', 'JAVASCRIPT&COLON;x', '\u0001javascript:x', '\u200bjavascript:x', 'jav\\nascript:x', 'jav%5cnascript:x', 'java%5Ctscript:x',
  'http:example.com/a/../b', 'http://>x.y', 'http://a b/', 'http://a|b/', 'http://a^b/', 'http://a<b>/', 'http://a[b/', 'http://a@b.c/', 'https://a:b@c.d/x', 'http://a#b', 'http://a?b', 'https:\\\\x.y\\z', 'http:///x', 'http:HOST:80/p q', 'https://x.y:443/', 'http://x.y:8080/a b?q=1 2#f g', 'http://x.y/%2e%2e/a', 'http://[::1]/x', 'http://user:pw@host.com/x', 'http://x.y/日本', 'https://x.y/a/./b/', 'http://x.y/a/..', 'http:x', 'http:', 'about:blank', '', ' ', '%', '%zz', '%E0%A4%A', '%C3%A9', 'a:b:c', 'x://y:z', 'abc', ':', '::', 'a:', ':a', 'line1\nhttp://x.y', 'text\njavascript:x', 'http://x.y&colon;80', '&#', '&#;', '&#x;', '&#1e3;', '&#0x41;', '&#65', '&#Infinity;', '&#abc;', '&#106;avascript:x', '%2f%2fevil.com', 'ht%74p://x.y', 'HtTp://X.y/Q', 'my link', 'https://a.b/c d'];
const lines = [];
const count = Number(process.argv[3] ?? 500);
const opts = [{}, { links: false }, { linkSchemes: ['https'] }, { linkSchemes: ['http', 'FTP'] }, { images: false }, { imageSchemes: ['https', 'data'] }, { linkRel: '' }, { linkRel: 'nofollow noopener' }];
for (let c = 0; c < count; c++) {
  let url = pick(URLS);
  if (rnd() < 0.3) url = pick(['', ' ', '\t', '%20', 'x']) + url + pick(['', ' ', '#x', '?q=1']);
  const o = pick(opts);
  const links = [];
  const body = '<g/>';
  const l = U.linked(body, U.sanitizeUrl(url), 'id&1', links, o, pick(['', ' target="_blank"']));
  const optsStr = JSON.stringify(o);
  lines.push(`url ${hex16(url)} ${hex16(U.sanitizeUrl(url))} ${hex16(U.safeUrl(url))} ${U.linkUrl(U.sanitizeUrl(url), o) === undefined ? '-' : hex16(U.linkUrl(U.sanitizeUrl(url), o))} ${U.imageUrl(U.sanitizeUrl(url), o) === undefined ? '-' : hex16(U.imageUrl(U.sanitizeUrl(url), o))} ${hex8(U.relAttr(o))} ${hex8(optsStr)}`);
}
fs.writeFileSync(process.argv[4] ?? 'tests/data/url_cases.txt', lines.join('\n') + '\n');
console.log('cases', lines.length);
