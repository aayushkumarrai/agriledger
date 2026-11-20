import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, statSync } from 'node:fs';
import { join } from 'node:path';
import { WEB, messages } from './helpers.mjs';
import { setMessages, t, hasMessage, applyI18n, loadMessages } from '../../web/js/i18n.js';

function files(dir, ext, out = []) {
  for (const name of readdirSync(dir)) {
    const p = join(dir, name);
    if (statSync(p).isDirectory()) files(p, ext, out); else if (p.endsWith(ext)) out.push(p);
  }
  return out;
}

function flatten(node, prefix = '', out = new Set()) {
  for (const [k, v] of Object.entries(node)) {
    if (typeof v === 'string') out.add(prefix + k); else flatten(v, `${prefix}${k}.`, out);
  }
  return out;
}

test('t() fills {placeholders} and reports problems loudly', () => {
  setMessages({ a: { b: 'Only {stock} left of {name}.' }, plain: 'Hello' });
  assert.equal(t('plain'), 'Hello');
  assert.equal(t('a.b', { stock: '12.0 quintal', name: 'paddy' }), 'Only 12.0 quintal left of paddy.');
  const logged = [];
  const real = console.error;
  console.error = (m) => logged.push(m);
  try {
    assert.equal(t('missing.key'), 'missing.key');
    assert.equal(t('a.b', { stock: 1 }), 'Only 1 left of {name}.');
    assert.equal(t('a'), 'a', 'a branch is not a message');
  } finally { console.error = real; }
  assert.equal(logged.length, 3);
  assert.ok(hasMessage('plain') && !hasMessage('nope'));
});

test('applyI18n sets text and attributes from data attributes', () => {
  setMessages({ x: 'Text', y: 'Label' });
  const el1 = { dataset: { i18n: 'x' }, textContent: '' };
  const el2 = { dataset: { i18nAttr: 'aria-label:y; title:x' }, attrs: {}, setAttribute(k, v) { this.attrs[k] = v; } };
  applyI18n({ querySelectorAll: (sel) => (sel === '[data-i18n]' ? [el1] : [el2]) });
  assert.equal(el1.textContent, 'Text');
  assert.deepEqual(el2.attrs, { 'aria-label': 'Label', title: 'Text' });
});

test('loadMessages fetches the catalogue and refuses a failed request', async () => {
  await loadMessages('/i18n/en.json', async () => ({ ok: true, json: async () => ({ q: 'ok' }) }));
  assert.equal(t('q'), 'ok');
  await assert.rejects(loadMessages('/x', async () => ({ ok: false })));
});

test('every message key used in HTML and JavaScript exists in en.json', () => {
  const known = flatten(messages);
  const used = new Map();
  const note = (key, where) => { if (!used.has(key)) used.set(key, where); };
  for (const f of files(WEB, '.html')) {
    const src = readFileSync(f, 'utf8');
    for (const m of src.matchAll(/data-i18n="([^"]+)"/g)) note(m[1], f);
    for (const m of src.matchAll(/data-i18n-attr="([^"]+)"/g)) for (const pair of m[1].split(';')) note(pair.split(':')[1]?.trim(), f);
  }
  for (const f of files(join(WEB, 'js'), '.js')) {
    const src = readFileSync(f, 'utf8').replace(/(^|\s)\/\/.*$/gm, '');   // comments may show example keys
    for (const m of src.matchAll(/\bt\(\s*'([\w.]+)'/g)) note(m[1], f);
    for (const m of src.matchAll(/\bt\(\s*`([\w.]+)`/g)) note(m[1], f);
    for (const m of src.matchAll(/data-i18n(?:-attr)?="([^"]+)"/g)) note(m[1], f);
    for (const m of src.matchAll(/i18nKey:\s*'([\w.]+)'/g)) note(m[1], f);
  }
  const missing = [...used].filter(([key]) => key && !known.has(key));
  assert.deepEqual(missing, [], `keys used but not in en.json: ${JSON.stringify(missing)}`);
  assert.ok(used.size > 30, 'the scan found the keys');
});

test('dynamic reason and input keys are all present', () => {
  for (const reason of ['OUT_OF_STOCK', 'INSUFFICIENT_STOCK', 'PRICE_CHANGED', 'BELOW_MIN_ORDER', 'POOL_NOT_OPEN', 'BUSY'])
    assert.ok(hasMessage.call(null, `reason.${reason}.title`) || flatten(messages).has(`reason.${reason}.title`), reason);
  for (const r of ['empty', 'invalid', 'zero', 'too_large']) assert.ok(flatten(messages).has(`input.quantity_${r}`), r);
  for (const r of ['empty', 'invalid', 'decimals', 'zero', 'too_large']) assert.ok(flatten(messages).has(`input.price_${r}`), r);
  assert.ok(flatten(messages).has('input.quantity_too_precise_kg') && flatten(messages).has('input.quantity_too_precise_quintal'));
});

test('no message shows an error code or number to the user', () => {
  for (const key of flatten(messages)) {
    const text = key.split('.').reduce((n, p) => n[p], messages);
    assert.ok(!/\bHTTP\b|(?<![\d,.])[45]\d\d(?![\d,.])/.test(text.replace(/\{\w+\}/g, '')), `${key} looks like it shows a status number: ${text}`);
    assert.ok(!/[A-Z]{2,}_[A-Z_]+/.test(text), `${key} shows a reason code: ${text}`);
  }
});
