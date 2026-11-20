import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { WEB } from './helpers.mjs';
import {
  toBigInt, padColumn, formatQuantityInput, formatPricePerQuintalWhole, groupIndian, formatPaise, formatPricePerQuintal, formatPricePerKg, formatQuantity, totalPaise, formatPermille,
  parseQuantity, parsePricePerQuintal, formatDateTime, formatDate, getUnit, setUnit, MAX_GRAMS,
} from '../../web/js/format.js';

test('Indian digit grouping', () => {
  const cases = { '0': '0', '999': '999', '1000': '1,000', '12500': '12,500', '100000': '1,00,000', '125000': '1,25,000',
    '1250000': '12,50,000', '123456789': '12,34,56,789', '1234567890123': '12,34,56,78,90,123' };
  for (const [input, want] of Object.entries(cases)) assert.equal(groupIndian(input), want);
});

test('grouping agrees with the browser en-IN formatter on whole numbers', () => {
  const fmt = new Intl.NumberFormat('en-IN');
  if (fmt.format(125000) !== '1,25,000') return;   // this runtime has no Indian locale data; nothing to compare
  for (const n of [0n, 7n, 999n, 1000n, 99999n, 100000n, 1234567n, 98765432123n]) assert.equal(groupIndian(n.toString()), fmt.format(n));
});

test('money: paise to rupees with two decimals', () => {
  assert.equal(formatPaise(1250000), '₹12,500.00');   // the worked example's gross
  assert.equal(formatPaise(375000), '₹3,750.00');
  assert.equal(formatPaise(500000), '₹5,000.00');
  assert.equal(formatPaise(0), '₹0.00');
  assert.equal(formatPaise(5), '₹0.05');
  assert.equal(formatPaise(100), '₹1.00');
  assert.equal(formatPaise(12500000), '₹1,25,000.00');
  assert.equal(formatPaise(1234567801n), '₹1,23,45,678.01');
  assert.equal(formatPaise(-375000), '−₹3,750.00');
  assert.equal(formatPaise(9007199254740993n), '₹9,00,71,99,25,47,409.93', 'beyond 2^53 stays exact');
});

test('prices: paise per kg shown per quintal and per kg', () => {
  assert.equal(formatPricePerQuintal(2500), '₹2,500.00');
  assert.equal(formatPricePerKg(2500), '₹25.00');
  assert.equal(formatPricePerQuintal(2441), '₹2,441.00');   // the paddy MSP, 244,100 paise per quintal
  assert.equal(formatPaise(244100), '₹2,441.00');
});

test('columns of numbers are padded to one number of decimals', () => {
  assert.deepEqual(padColumn(['9.75', '10.0', '4.5']), ['9.75', '10.00', '4.50']);
  assert.deepEqual(padColumn(['+3.0', '\u22121.25', '12,000.5']), ['+3.00', '\u22121.25', '12,000.50']);
  assert.deepEqual(padColumn(['15.0', '4.5']), ['15.0', '4.5']);
  assert.deepEqual(padColumn(['\u2013', '2.25', '3.5']), ['\u2013', '2.25', '3.50']);
  assert.deepEqual(padColumn(['5', '2.5']), ['5.0', '2.5']);
  assert.deepEqual(padColumn([]), []);
});

test('whole-rupee prices for cards', () => {
  assert.equal(formatPricePerQuintalWhole(2500), '₹2,500');
  assert.equal(formatPricePerQuintalWhole(125000), '₹1,25,000');
});

test('quantity: quintal with up to 2 decimals, kg with up to 3, trailing zeros trimmed to one decimal', () => {
  const quintal = { 1500000: '15.0', 450000: '4.5', 600000: '6.0', 2000000: '20.0', 500000: '5.0', 0: '0.0', 100000: '1.0',
    123400: '1.23', 125000: '1.25', 1000: '0.01', 500: '0.01', 499: '0.0', 150: '0.0', 10050000: '100.5', 1234567800: '12,345.68' };
  for (const [g, want] of Object.entries(quintal)) assert.equal(formatQuantity(Number(g), 'quintal'), want, `${g} g in quintal`);
  const kg = { 1500: '1.5', 500000: '500.0', 1: '0.001', 33: '0.033', 1250: '1.25', 1000: '1.0', 0: '0.0' };
  for (const [g, want] of Object.entries(kg)) assert.equal(formatQuantity(Number(g), 'kg'), want, `${g} g in kg`);
  assert.equal(formatQuantity(-150000, 'quintal'), '−1.5');
  assert.equal(formatQuantity(-1, 'quintal'), '0.0', 'a negative that rounds to nothing has no sign');
  assert.equal(formatQuantity(10n ** 15n, 'quintal'), '10,00,00,00,000.0');
});

test('input text is exact, ungrouped and round-trips through the parser', () => {
  assert.equal(formatQuantityInput(500000, 'quintal'), '5');
  assert.equal(formatQuantityInput(550000, 'quintal'), '5.5');
  assert.equal(formatQuantityInput(1, 'quintal'), '0.00001');
  assert.equal(formatQuantityInput(5, 'kg'), '0.005');
  assert.equal(formatQuantityInput(1500, 'kg'), '1.5');
  assert.equal(formatQuantityInput(12500000000, 'quintal'), '125000');
  for (const g of [1n, 7n, 999n, 1000n, 123457n, 500000n, 999999999999999n]) {
    for (const unit of ['kg', 'quintal']) assert.deepEqual(parseQuantity(formatQuantityInput(g, unit), unit), { ok: true, grams: g }, `${g} ${unit}`);
  }
});

test('rounding half-up matches the server rule: gross = round_half_up(grams x price / 1000)', () => {
  assert.equal(totalPaise(500000, 2500), 1250000n);   // master §12 worked example
  assert.equal(totalPaise(100, 2500), 250n);          // master §12 rounding example
  assert.equal(totalPaise(1, 500), 1n);               // 0.5 paise rounds up
  assert.equal(totalPaise(1, 499), 0n);
  assert.equal(totalPaise(3, 1500), 5n);              // 4.5 -> 5
  assert.equal(totalPaise(10n ** 15n, 10n ** 9n), 10n ** 21n, 'exact far beyond 2^53');
});

test('shares: one decimal, trailing zero trimmed', () => {
  const cases = { 300: '30%', 400: '40%', 368: '36.8%', 141: '14.1%', 1000: '100%', 0: '0%', 5: '0.5%', 995: '99.5%' };
  for (const [p, want] of Object.entries(cases)) assert.equal(formatPermille(Number(p)), want);
});

test('quantity text is read digit by digit (no floating point)', () => {
  const q = (text, unit) => parseQuantity(text, unit);
  assert.deepEqual(q('5', 'quintal'), { ok: true, grams: 500000n });
  assert.deepEqual(q('5.5', 'quintal'), { ok: true, grams: 550000n });
  assert.deepEqual(q('0.00001', 'quintal'), { ok: true, grams: 1n });
  assert.deepEqual(q('.5', 'quintal'), { ok: true, grams: 50000n });
  assert.deepEqual(q('5.', 'quintal'), { ok: true, grams: 500000n });
  assert.deepEqual(q(' 1,25,000 ', 'quintal'), { ok: true, grams: 12500000000n });
  assert.deepEqual(q('0.1', 'quintal'), { ok: true, grams: 10000n }, 'no 0.1 rounding trouble');
  assert.deepEqual(q('0.3', 'quintal'), { ok: true, grams: 30000n });
  assert.deepEqual(q('1.005', 'kg'), { ok: true, grams: 1005n });
  assert.deepEqual(q('0.001', 'kg'), { ok: true, grams: 1n });
  assert.equal(q('9007199254740993', 'kg').reason, 'too_large');
  assert.deepEqual(q('9999999999.99999', 'quintal'), { ok: true, grams: 999999999999999n }, 'exact, no rounding at the top of the range');
  assert.deepEqual(q('10000000000', 'quintal'), { ok: true, grams: MAX_GRAMS });
  assert.equal(q('10000000001', 'quintal').reason, 'too_large');
  assert.equal(q('5.000001', 'quintal').reason, 'too_precise');
  assert.equal(q('5.5555', 'kg').reason, 'too_precise');
  assert.equal(q('', 'kg').reason, 'empty');
  assert.equal(q('   ', 'kg').reason, 'empty');
  assert.equal(q('0', 'quintal').reason, 'zero');
  assert.equal(q('0.0', 'quintal').reason, 'zero');
  for (const bad of ['abc', '.', '-5', '1e3', '5..5', '5.5.5', '₹5', '5q', '--', 'NaN', 'Infinity']) assert.equal(q(bad, 'quintal').reason, 'invalid', bad);
});

test('commas and spaces are accepted only where they mean thousands', () => {
  const q = (text) => parseQuantity(text, 'quintal');
  assert.deepEqual(q('1,000'), { ok: true, grams: 100000000n });
  assert.deepEqual(q('1,25,000.5'), { ok: true, grams: 12500050000n });
  assert.deepEqual(q(' 5 '), { ok: true, grams: 500000n });
  for (const bad of ['2,5', '5 5', '12,34', '1,2,3', ',5', '5,', '5.5,5', '1 000', '1,0000', '1,,000'])
    assert.equal(q(bad).reason, 'invalid', `${bad} must not be read as a number`);
  assert.equal(q('12,34,567').ok, true, 'Indian grouping');
  assert.equal(q('1,234,567').ok, true, 'western grouping');
  assert.equal(parsePricePerQuintal('2,5').reason, 'invalid');
  assert.equal(parsePricePerQuintal('2 500').reason, 'invalid');
  assert.deepEqual(parsePricePerQuintal('2,500'), { ok: true, paisePerKg: 2500n });
});

test('price text: whole rupees per quintal only', () => {
  const p = (text) => parsePricePerQuintal(text);
  assert.deepEqual(p('2500'), { ok: true, paisePerKg: 2500n });
  assert.deepEqual(p('2,500'), { ok: true, paisePerKg: 2500n });
  assert.deepEqual(p('2,500.00'), { ok: true, paisePerKg: 2500n }, 'the wireframe shows 2,500.00');
  assert.deepEqual(p('2500.0'), { ok: true, paisePerKg: 2500n });
  assert.deepEqual(p('2500.'), { ok: true, paisePerKg: 2500n });
  assert.equal(p('2,500.50').reason, 'decimals');
  assert.equal(p('2500.01').reason, 'decimals');
  assert.equal(p('2500.5').reason, 'decimals');
  assert.equal(p('').reason, 'empty');
  assert.equal(p('0').reason, 'zero');
  assert.equal(p('0.00').reason, 'zero');
  assert.equal(p('1000000001').reason, 'too_large');
  assert.deepEqual(p('1000000000'), { ok: true, paisePerKg: 1000000000n });
  for (const bad of ['abc', '.5', '-1', '₹2500', '2500q']) assert.equal(p(bad).reason, 'invalid', bad);
});

test('toBigInt accepts exact integers only', () => {
  assert.equal(toBigInt(5), 5n);
  assert.equal(toBigInt('12'), 12n);
  assert.equal(toBigInt(-3n), -3n);
  for (const bad of [1.5, NaN, Infinity, 2 ** 53, '1.5', 'abc', null, undefined, {}]) assert.throws(() => toBigInt(bad), undefined, String(bad));
});

test('dates', () => {
  assert.equal(formatDateTime('2026-11-17T11:12:00Z', 'UTC'), '17 Nov, 11:12');
  assert.equal(formatDateTime('2026-11-17T11:12:00Z', 'Asia/Kolkata'), '17 Nov, 16:42', 'the wireframe sale time');
  assert.equal(formatDateTime('2026-11-03T10:20:00Z', 'Asia/Kolkata'), '03 Nov, 15:50');
  assert.equal(formatDate('2026-11-03'), '03 Nov');
});

test('the unit choice is remembered in the browser and survives a broken store', () => {
  const real = globalThis.localStorage;
  try {
    const store = new Map();
    globalThis.localStorage = { getItem: (k) => (store.has(k) ? store.get(k) : null), setItem: (k, v) => store.set(k, v) };
    assert.equal(getUnit(), 'quintal', 'quintal is the default');
    setUnit('kg');
    assert.equal(getUnit(), 'kg');
    setUnit('quintal');
    assert.equal(getUnit(), 'quintal');
    store.set('agriledger.unit', 'tonnes');
    assert.equal(getUnit(), 'quintal', 'an unknown stored value falls back to quintal');
    globalThis.localStorage = { getItem() { throw new Error('blocked'); }, setItem() { throw new Error('blocked'); } };
    assert.equal(getUnit(), 'quintal');
    assert.doesNotThrow(() => setUnit('kg'));
  } finally {
    if (real === undefined) delete globalThis.localStorage; else globalThis.localStorage = real;
  }
});

test('static rule: format.js has no floating-point arithmetic', () => {
  const src = readFileSync(join(WEB, 'js', 'format.js'), 'utf8').split('\n').filter((l) => !l.trim().startsWith('//')).join('\n');
  for (const banned of [/parseFloat/, /\.toFixed\(/, /Math\./, /parseInt/, /\bNumber\.parse/, /\d\.\d+(?![\d'"`])(?!\.)/]) {
    const hit = banned.exec(src.replace(/'[^']*'|`[^`]*`|\/[^/\n]+\/[a-z]*/g, ''));
    assert.equal(hit, null, `banned pattern ${banned} found: ${hit && hit[0]}`);
  }
});
