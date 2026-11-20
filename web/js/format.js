// Exact number handling for quantity and money (CLAUDE.md rule 1). No floating point:
// everything is BigInt. Quantities are integer grams, money is integer paise, prices are integer paise per kg
// (which is the same number as rupees per quintal). Text typed by a person is parsed digit by digit, never with
// parseFloat or Number(); display text is built from integer division and remainders.
//
// Plain JSON numbers from the API arrive as JavaScript numbers, which are exact for integers up to 2^53 (about 9 x 10^15,
// far above any gram or paise value here). toBigInt() accepts those and refuses anything that is not an exact integer.

export const GRAMS_PER_KG = 1000n;
export const GRAMS_PER_QUINTAL = 100000n;
export const MAX_GRAMS = 1000000000000000n;   // the server's limit per order and per deposit
export const MAX_PRICE = 1000000000n;         // paise per kg, the server's limit

const MINUS = '−';
const RUPEE = '₹';

export function toBigInt(value) {
  if (typeof value === 'bigint') return value;
  if (typeof value === 'number') {
    if (!Number.isSafeInteger(value)) throw new RangeError('not an exact integer');
    return BigInt(value);
  }
  if (typeof value === 'string' && /^-?\d+$/.test(value)) return BigInt(value);
  throw new TypeError('expected an integer');
}

// "1250000" -> "12,50,000": the last three digits, then groups of two (Indian grouping).
export function groupIndian(digits) {
  if (digits.length <= 3) return digits;
  let rest = digits.slice(0, -3);
  const groups = [];
  while (rest.length > 2) { groups.unshift(rest.slice(-2)); rest = rest.slice(0, -2); }
  if (rest) groups.unshift(rest);
  return `${groups.join(',')},${digits.slice(-3)}`;
}

function split(value) {
  const n = toBigInt(value);
  return n < 0n ? ['neg', -n] : ['pos', n];
}

// Paise as rupees with two decimals: 1250000 -> "₹12,500.00"; negative amounts use a real minus sign.
export function formatPaise(paise) {
  const [sign, n] = split(paise);
  const text = `${RUPEE}${groupIndian((n / 100n).toString())}.${(n % 100n).toString().padStart(2, '0')}`;
  return sign === 'neg' && n !== 0n ? MINUS + text : text;
}

// A price stored as paise per kg, shown per quintal: 2500 -> "₹2,500.00" (the same number, as 100 kg x p paise = p rupees).
export function formatPricePerQuintal(paisePerKg) {
  return formatPaise(toBigInt(paisePerKg) * 100n);
}

// A price per quintal in whole rupees, as on the pool cards: 2500 -> "₹2,500". Prices are always whole rupees per quintal.
export function formatPricePerQuintalWhole(paisePerKg) {
  return `${RUPEE}${groupIndian(toBigInt(paisePerKg).toString())}`;
}

// The same price per kg: 2500 -> "₹25.00".
export function formatPricePerKg(paisePerKg) {
  return formatPaise(paisePerKg);
}

// Reference prices are stored as paise per quintal.
export function formatPaisePerQuintal(paise) {
  return formatPaise(paise);
}

// Quantity from grams. Quintal: up to 2 decimals; kg: up to 3. Rounded half-up, trailing zeros trimmed to at least one
// decimal: 1500000 g -> "15.0" quintal, 450000 g -> "4.5", 1500 g -> "1.5" kg.
export function formatQuantity(grams, unit = 'quintal') {
  const [sign, n] = split(grams);
  const decimals = unit === 'kg' ? 3 : 2;
  const gramsPerStep = (unit === 'kg' ? GRAMS_PER_KG : GRAMS_PER_QUINTAL) / 10n ** BigInt(decimals);
  const steps = (n + gramsPerStep / 2n) / gramsPerStep;
  const factor = 10n ** BigInt(decimals);
  let fraction = (steps % factor).toString().padStart(decimals, '0').replace(/0+$/, '');
  if (fraction === '') fraction = '0';
  const text = `${groupIndian((steps / factor).toString())}.${fraction}`;
  return sign === 'neg' && steps !== 0n ? MINUS + text : text;
}

// Grams as editable input text, exact and ungrouped: 550000 g -> "5.5" quintal, 5 g -> "0.005" kg. Nothing is rounded.
export function formatQuantityInput(grams, unit = 'quintal') {
  const n = toBigInt(grams);
  const decimals = unit === 'kg' ? 3 : 5;
  const perUnit = unit === 'kg' ? GRAMS_PER_KG : GRAMS_PER_QUINTAL;
  const whole = n / perUnit;
  const fraction = ((n % perUnit) * 10n ** BigInt(decimals) / perUnit).toString().padStart(decimals, '0').replace(/0+$/, '');
  return fraction === '' ? whole.toString() : `${whole}.${fraction}`;
}

// gross = round_half_up(grams x paise-per-kg / 1000), rounded once: the same rule the server uses.
export function totalPaise(grams, paisePerKg) {
  return (toBigInt(grams) * toBigInt(paisePerKg) + 500n) / 1000n;
}

// A share given in permille: 300 -> "30%", 368 -> "36.8%" (one decimal, trailing zero trimmed).
export function formatPermille(permille) {
  const n = toBigInt(permille);
  const whole = n / 10n;
  const tenth = n % 10n;
  return tenth === 0n ? `${whole}%` : `${whole}.${tenth}%`;
}

// ---- reading what a person typed ------------------------------------------------------------------------------------

// Trims the text and drops thousands commas, but only commas that sit where a thousands separator belongs (1,000 or the Indian
// 1,25,000). Anything else is refused, so "2,5" is not silently read as 25 and "5 5" is not read as 55. Returns null if refused.
function normalise(text) {
  const t = String(text ?? '').trim();
  if (/\s/.test(t)) return null;
  const dot = t.indexOf('.');
  const whole = dot === -1 ? t : t.slice(0, dot);
  const fraction = dot === -1 ? '' : t.slice(dot + 1);
  if (fraction.includes(',')) return null;
  if (whole.includes(',') && !/^\d{1,3}(,\d{2})*,\d{3}$|^\d{1,3}(,\d{3})+$/.test(whole)) return null;
  return t.replace(/,/g, '');
}

// Quantity text in the chosen unit -> { ok: true, grams } or { ok: false, reason }.
// Reasons: empty, invalid, zero, too_precise, too_large. Kg allows 3 decimals, quintal 5 (both are whole grams).
export function parseQuantity(text, unit = 'quintal') {
  const t = normalise(text);
  if (t === null) return { ok: false, reason: 'invalid' };
  if (t === '') return { ok: false, reason: 'empty' };
  const m = /^(\d*)(?:\.(\d*))?$/.exec(t);
  if (!m || (m[1] === '' && (m[2] === undefined || m[2] === ''))) return { ok: false, reason: 'invalid' };
  const decimals = unit === 'kg' ? 3 : 5;
  const whole = m[1] === '' ? 0n : BigInt(m[1]);
  const fraction = m[2] ?? '';
  if (fraction.length > decimals) return { ok: false, reason: 'too_precise' };
  const perUnit = unit === 'kg' ? GRAMS_PER_KG : GRAMS_PER_QUINTAL;
  const grams = whole * perUnit + BigInt(fraction.padEnd(decimals, '0') || '0') * (perUnit / 10n ** BigInt(decimals));
  if (grams === 0n) return { ok: false, reason: 'zero' };
  if (grams > MAX_GRAMS) return { ok: false, reason: 'too_large' };
  return { ok: true, grams };
}

// Price text in rupees per quintal -> { ok: true, paisePerKg } or { ok: false, reason }.
// Whole rupees only; "2,500" and "2,500.00" are fine, "2,500.50" is refused (reason: decimals).
export function parsePricePerQuintal(text) {
  const t = normalise(text);
  if (t === null) return { ok: false, reason: 'invalid' };
  if (t === '') return { ok: false, reason: 'empty' };
  const m = /^(\d+)(?:\.(\d*))?$/.exec(t);
  if (!m) return { ok: false, reason: 'invalid' };
  if (m[2] !== undefined && /[1-9]/.test(m[2])) return { ok: false, reason: 'decimals' };
  const rupees = BigInt(m[1]);
  if (rupees === 0n) return { ok: false, reason: 'zero' };
  if (rupees > MAX_PRICE) return { ok: false, reason: 'too_large' };
  return { ok: true, paisePerKg: rupees };   // rupees per quintal is the same number as paise per kg
}

// ---- dates (display only) -------------------------------------------------------------------------------------------

const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];

// "2026-11-17T11:12:00Z" -> "17 Nov, 16:42" in the viewer's time zone (or the one given, e.g. "Asia/Kolkata").
export function formatDateTime(iso, timeZone) {
  const parts = new Intl.DateTimeFormat('en-GB', {
    day: '2-digit', month: 'numeric', hour: '2-digit', minute: '2-digit', hourCycle: 'h23', ...(timeZone ? { timeZone } : {}),
  }).formatToParts(new Date(iso));
  const get = (type) => parts.find((p) => p.type === type).value;
  return `${get('day')} ${MONTHS[Number(get('month')) - 1]}, ${get('hour')}:${get('minute')}`;
}

// "2026-11-03" -> "03 Nov"
export function formatDate(isoDate) {
  const [, month, day] = isoDate.split('-');
  return `${day} ${MONTHS[Number(month) - 1]}`;
}

// ---- the kg / quintal choice lives in the browser, not on the server -----------------------------------------------

const UNIT_KEY = 'agriledger.unit';

export function getUnit() {
  try { return localStorage.getItem(UNIT_KEY) === 'kg' ? 'kg' : 'quintal'; } catch { return 'quintal'; }
}

export function setUnit(unit) {
  try { localStorage.setItem(UNIT_KEY, unit === 'kg' ? 'kg' : 'quintal'); } catch { /* private window: the choice just is not remembered */ }
}
