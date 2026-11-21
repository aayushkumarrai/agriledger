// Thin fetch wrapper for the REST API (spec §6). Sends and receives JSON, keeps the session cookie (same origin),
// and turns every failure into an ApiError that carries the server's reason code and data. Codes are never shown:
// describeError() maps them to the plain sentences in i18n/en.json (CLAUDE.md rule 4).
import { t } from './i18n.js';
import { formatQuantity, formatPricePerQuintal, formatPaise, totalPaise, toBigInt } from './format.js';

export class ApiError extends Error {
  constructor(status, code, data = {}) {
    super(code || `HTTP ${status}`);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;      // for example "INVALID_FIELD", or for a refused sale the reason "OUT_OF_STOCK"
    this.data = data;      // the rest of the response body (stock_g, current_price_paise_per_kg, min_order_g ...)
  }
}

// Called once when the server says the session is gone (401) so the page can go to the login screen.
let onSessionEnded = () => {};
export function setSessionEndedHandler(fn) { onSessionEnded = fn; }

export function newRequestId() {
  if (globalThis.crypto && typeof globalThis.crypto.randomUUID === 'function') return globalThis.crypto.randomUUID();
  const b = new Uint8Array(16);
  globalThis.crypto.getRandomValues(b);
  b[6] = (b[6] & 0x0f) | 0x40;
  b[8] = (b[8] & 0x3f) | 0x80;
  const h = [...b].map((x) => x.toString(16).padStart(2, '0'));
  return `${h.slice(0, 4).join('')}-${h.slice(4, 6).join('')}-${h.slice(6, 8).join('')}-${h.slice(8, 10).join('')}-${h.slice(10).join('')}`;
}

async function request(method, path, { body, form, fetchImpl = fetch } = {}) {
  const init = { method, credentials: 'same-origin', headers: {} };
  if (body !== undefined) { init.headers['Content-Type'] = 'application/json'; init.body = JSON.stringify(body); }
  if (form !== undefined) init.body = form;   // the browser sets the multipart boundary itself
  let res;
  try {
    res = await fetchImpl(path, init);
  } catch {
    throw new ApiError(0, 'NETWORK');
  }
  let data = {};
  try { data = await res.json(); } catch { /* an empty or non-JSON body */ }
  if (res.ok) return data;
  const code = data.reason || data.error || '';
  if (res.status === 401) onSessionEnded();
  throw new ApiError(res.status, code, data);
}

export const api = {
  get: (path, opts) => request('GET', path, opts),
  post: (path, body, opts) => request('POST', path, { body, ...opts }),
  patch: (path, body, opts) => request('PATCH', path, { body, ...opts }),
  upload: (path, file, opts) => { const form = new FormData(); form.append('photo', file); return request('POST', path, { form, ...opts }); },
};

// A sentence for a person, from an ApiError (or anything else that was thrown). `unit` is 'kg' or 'quintal';
// `order` (optional) is { grams, paisePerKg } of the order that was refused, used by the price-changed message.
export function describeError(err, unit = 'quintal', order = null) {
  if (!(err instanceof ApiError)) return { title: t('error.generic'), body: '', action: '' };
  if (err.status === 401) return { title: t('error.session_ended'), body: '', action: '' };
  const d = err.data || {};
  const qty = (g) => `${formatQuantity(g, unit)} ${t(`unit.${unit}`)}`;
  switch (err.code) {
    case 'OUT_OF_STOCK':
      return { title: t('reason.OUT_OF_STOCK.title'), body: t('reason.OUT_OF_STOCK.body'), action: '' };
    case 'INSUFFICIENT_STOCK':
      return { title: t('reason.INSUFFICIENT_STOCK.title', { stock: qty(toBigInt(d.stock_g)) }), body: t('reason.INSUFFICIENT_STOCK.body'),
               action: t('reason.INSUFFICIENT_STOCK.action', { stock: qty(toBigInt(d.stock_g)) }) };
    case 'PRICE_CHANGED': {
      const now = toBigInt(d.current_price_paise_per_kg);
      const old = order ? toBigInt(order.paisePerKg) : null;
      const body = order
        ? t('reason.PRICE_CHANGED.body', { old: formatPricePerQuintal(old), new: formatPricePerQuintal(now), total: formatPaise(totalPaise(order.grams, now)), quantity: qty(toBigInt(order.grams)) })
        : t('reason.PRICE_CHANGED.note');
      return { title: t('reason.PRICE_CHANGED.title'), body, note: t('reason.PRICE_CHANGED.note'), action: t('reason.PRICE_CHANGED.action') };
    }
    case 'BELOW_MIN_ORDER':
      return { title: t('reason.BELOW_MIN_ORDER.title', { min: qty(toBigInt(d.min_order_g)) }), body: '', action: '' };
    case 'BUYER_NOT_APPROVED':
      return { title: t('reason.BUYER_NOT_APPROVED.title'), body: '', action: '' };
    case 'POOL_NOT_OPEN':
      return { title: t('reason.POOL_NOT_OPEN.title'), body: '', action: '' };
    case 'BUSY':
      return { title: t('reason.BUSY.title'), body: t('reason.BUSY.body'), action: t('reason.BUSY.action') };
    default:
      if (err.status === 403) return { title: t('error.forbidden'), body: '', action: '' };
      if (err.status === 404) return { title: t('error.not_found'), body: '', action: '' };
      return { title: t('error.generic'), body: '', action: '' };
  }
}
