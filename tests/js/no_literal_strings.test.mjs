// CLAUDE.md rule 4: no literal UI strings in HTML or JavaScript. Every visible word comes from i18n/en.json by key.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { WEB } from './helpers.mjs';

function files(dir, ext, out = []) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) files(p, ext, out); else if (p.endsWith(ext)) out.push(p);
  }
  return out;
}

test('HTML: every element with text carries data-i18n, and no attribute shows a literal string', () => {
  const problems = [];
  for (const f of files(WEB, '.html')) {
    const src = readFileSync(f, 'utf8').replace(/<!--[\s\S]*?-->/g, '').replace(/<(script|style)[\s\S]*?<\/\1>/g, '');
    for (const m of src.matchAll(/<(\w+)([^>]*)>([^<]*)/g)) {
      const [, tag, attrs, text] = m;
      if (tag === 'title' || /data-i18n=/.test(attrs) || text.trim() === '') {
        if (/\s(alt|title|placeholder|aria-label)="[^"]+"/.test(attrs)) problems.push(`${f}: literal attribute in <${tag}${attrs}>`);
        continue;
      }
      problems.push(`${f}: literal text "${text.trim().slice(0, 40)}" in <${tag}>`);
    }
    for (const m of src.matchAll(/<title([^>]*)>([^<]*)/g)) if (!/data-i18n=/.test(m[1]) || m[2].trim() !== '') problems.push(`${f}: <title> must be filled from data-i18n`);
    if (/\sstyle="/.test(src)) problems.push(`${f}: inline style attribute (the Content-Security-Policy forbids it)`);
    if (/\son\w+="/.test(src)) problems.push(`${f}: inline event handler (the Content-Security-Policy forbids it)`);
    if (/<script(?![^>]*\bsrc=)[^>]*>/.test(readFileSync(f, 'utf8'))) problems.push(`${f}: inline script (the Content-Security-Policy forbids it)`);
  }
  assert.deepEqual(problems, []);
});

// The literal text of the string or template that starts at src[i] (an opening quote or backtick): everything outside ${...}.
function literalText(src, i) {
  const quote = src[i];
  let text = '';
  let depth = 0;
  for (let j = i + 1; j < src.length; j += 1) {
    const c = src[j];
    if (depth === 0) {
      if (c === '\\') { j += 1; continue; }
      if (c === quote) return text;
      if (quote === '`' && c === '$' && src[j + 1] === '{') { depth = 1; j += 1; continue; }
      text += c;
    } else if (c === '{') depth += 1;
    else if (c === '}') depth -= 1;
    else if (c === '`' || c === "'" || c === '"') {   // a string inside the ${...} expression: skip it whole
      const end = src.indexOf(c, j + 1);
      if (c === '`') { let d = 0; let k = j + 1; for (; k < src.length; k += 1) { if (src[k] === '$' && src[k + 1] === '{') d += 1; else if (src[k] === '}' && d > 0) d -= 1; else if (src[k] === '`' && d === 0) break; } j = k; }
      else j = end;
    }
  }
  return text;
}

test('JavaScript: no sentence-like literal is put on the page', () => {
  const problems = [];
  for (const f of files(join(WEB, 'js'), '.js')) {
    const src = readFileSync(f, 'utf8').replace(/\/\*[\s\S]*?\*\//g, '').replace(/(^|\s)\/\/.*$/gm, '');
    for (const m of src.matchAll(/(textContent|innerText|innerHTML|placeholder|alert|document\.title)\s*(?:=|\()\s*(?=['"`])/g)) {
      const text = literalText(src, m.index + m[0].length);
      if (/[A-Za-z]{3,}/.test(text.replace(/<[^>]*>/g, ''))) problems.push(`${f}: ${m[1]} = "${text.slice(0, 50)}"`);
    }
    for (const m of src.matchAll(/setAttribute\(\s*'(aria-label|title|placeholder|alt)'\s*,\s*(?=['"`])/g)) {
      const text = literalText(src, m.index + m[0].length);
      if (/[A-Za-z]{3,}/.test(text)) problems.push(`${f}: ${m[1]} = "${text.slice(0, 50)}"`);
    }
  }
  assert.deepEqual(problems, []);
});

test('the checker itself catches a literal sentence', () => {
  assert.match(literalText('x = "Pool is open"; y', 4), /Pool is open/);
  assert.equal(literalText('x = `${a} ${t(`k.${u}`)}`;', 4).trim(), '');
  assert.match(literalText('x = `Total: ${a}`;', 4), /Total/);
});
