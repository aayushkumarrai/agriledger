import test from 'node:test';
import assert from 'node:assert/strict';
import { messages } from './helpers.mjs';
import { setMessages } from '../../web/js/i18n.js';
import { api, ApiError, describeError, newRequestId, setSessionEndedHandler } from '../../web/js/api.js';

setMessages(messages);

const reply = (status, body) => async () => ({ ok: status >= 200 && status < 300, status, json: async () => body });

test('successful requests return the JSON; JSON is sent with the right header', async () => {
  let seen;
  const fetchImpl = async (path, init) => { seen = { path, init }; return { ok: true, status: 200, json: async () => ({ ok: 1 }) }; };
  assert.deepEqual(await api.post('/pools', { a: 1 }, { fetchImpl }), { ok: 1 });
  assert.equal(seen.init.method, 'POST');
  assert.equal(seen.init.headers['Content-Type'], 'application/json');
  assert.equal(seen.init.body, '{"a":1}');
  assert.equal(seen.init.credentials, 'same-origin');
  await api.get('/pools', { fetchImpl });
  assert.equal(seen.init.body, undefined);
});

test('a refused sale becomes an ApiError carrying the reason and its data', async () => {
  await assert.rejects(api.post('/pools/1/sales', {}, { fetchImpl: reply(409, { status: 'REJECTED', reason: 'INSUFFICIENT_STOCK', stock_g: 1200000 }) }),
    (e) => e instanceof ApiError && e.status === 409 && e.code === 'INSUFFICIENT_STOCK' && e.data.stock_g === 1200000);
  await assert.rejects(api.get('/x', { fetchImpl: reply(400, { error: 'INVALID_FIELD' }) }), (e) => e.code === 'INVALID_FIELD');
});

test('401 tells the page the session ended; network failure is status 0', async () => {
  let ended = 0;
  setSessionEndedHandler(() => { ended += 1; });
  await assert.rejects(api.get('/pools', { fetchImpl: reply(401, { error: 'UNAUTHENTICATED' }) }), (e) => e.status === 401);
  assert.equal(ended, 1);
  await assert.rejects(api.get('/pools', { fetchImpl: async () => { throw new TypeError('offline'); } }), (e) => e.status === 0 && e.code === 'NETWORK');
  assert.equal(ended, 1);
});

test('upload sends multipart without a hand-made content type', async () => {
  let init;
  await api.upload('/pools/1/photos', new Blob(['x'], { type: 'image/png' }), { fetchImpl: async (p, i) => { init = i; return { ok: true, status: 201, json: async () => ({}) }; } });
  assert.ok(init.body instanceof FormData);
  assert.equal(init.headers['Content-Type'], undefined);
});

test('request ids are random version-4 UUIDs, with a fallback when randomUUID is missing', () => {
  const v4 = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;
  const a = newRequestId(), b = newRequestId();
  assert.match(a, v4);
  assert.notEqual(a, b);
  const real = globalThis.crypto.randomUUID;
  Object.defineProperty(globalThis.crypto, 'randomUUID', { value: undefined, configurable: true });
  try { assert.match(newRequestId(), v4); } finally { Object.defineProperty(globalThis.crypto, 'randomUUID', { value: real, configurable: true }); }
});

test('every refusal becomes a plain sentence with no code or status number', () => {
  const err = (status, code, data = {}) => new ApiError(status, code, data);
  assert.equal(describeError(err(409, 'OUT_OF_STOCK'), 'quintal').title, 'This pool is out of stock.');
  const some = describeError(err(409, 'INSUFFICIENT_STOCK', { stock_g: 1200000 }), 'quintal');
  assert.equal(some.title, 'Only 12.0 quintal is available right now.');
  assert.equal(some.action, 'Buy remaining 12.0 quintal');
  assert.equal(describeError(err(409, 'INSUFFICIENT_STOCK', { stock_g: 1200000 }), 'kg').title, 'Only 1,200.0 kg is available right now.');
  const price = describeError(err(409, 'PRICE_CHANGED', { current_price_paise_per_kg: 2400 }), 'quintal', { grams: 500000, paisePerKg: 2500 });
  assert.equal(price.title, 'The price has changed.');
  assert.equal(price.body, 'From ₹2,500.00 to ₹2,400.00 per quintal. Your total is now ₹12,000.00 for 5.0 quintal.');
  assert.equal(price.action, 'Review and confirm again');
  assert.equal(describeError(err(409, 'BELOW_MIN_ORDER', { min_order_g: 100000 }), 'quintal').title, 'The minimum order for this pool is 1.0 quintal.');
  assert.equal(describeError(err(409, 'POOL_NOT_OPEN'), 'quintal').title, 'This pool is not open for sale.');
  const busy = describeError(err(503, 'BUSY'), 'quintal');
  assert.equal(busy.title, 'Many buyers are ordering right now.');
  assert.equal(busy.action, 'Try again');
  assert.equal(describeError(err(401, 'UNAUTHENTICATED')).title, 'Your session has ended. Please log in again.');
  assert.equal(describeError(err(500, 'INTERNAL')).title, 'Something went wrong. Please try again.');
  assert.equal(describeError(err(400, 'INVALID_FIELD')).title, 'Something went wrong. Please try again.');
  assert.equal(describeError(new Error('x')).title, 'Something went wrong. Please try again.');
  for (const e of [err(409, 'OUT_OF_STOCK'), err(503, 'BUSY'), err(500, 'INTERNAL'), err(403, 'FORBIDDEN'), err(0, 'NETWORK')]) {
    const text = Object.values(describeError(e)).join(' ');
    assert.ok(!/OUT_OF_STOCK|BUSY|INTERNAL|FORBIDDEN|NETWORK|\b50[03]\b|\b40[0-9]\b/.test(text), text);
  }
});
