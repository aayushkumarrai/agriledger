import test from 'node:test';
import assert from 'node:assert/strict';
import { LiveFeed, liveUrl } from '../../web/js/live.js';

class FakeSocket {
  static instances = [];
  constructor(url) { this.url = url; this.closed = false; FakeSocket.instances.push(this); }
  close() { this.closed = true; }
  open() { this.onopen && this.onopen(); }
  receive(obj) { this.onmessage && this.onmessage({ data: typeof obj === 'string' ? obj : JSON.stringify(obj) }); }
  drop() { this.onclose && this.onclose(); }
}

function setup(extra = {}) {
  FakeSocket.instances = [];
  const timers = [];
  const statuses = [];
  const resyncs = [];
  const feed = new LiveFeed({
    url: 'ws://test/live', WebSocketImpl: FakeSocket, onStatus: (s) => statuses.push(s),
    onResync: async () => { resyncs.push('start'); await new Promise((r) => setImmediate(r)); resyncs.push('done'); },
    setTimer: (fn, ms) => { timers.push({ fn, ms }); return timers.length; }, clearTimer: () => {}, ...extra,
  });
  return { feed, timers, statuses, resyncs, sock: () => FakeSocket.instances.at(-1) };
}

const tick = () => new Promise((r) => setImmediate(r));

test('the URL follows the page protocol', () => {
  assert.equal(liveUrl({ protocol: 'http:', host: 'localhost:8080' }), 'ws://localhost:8080/live');
  assert.equal(liveUrl({ protocol: 'https:', host: 'agri.example' }), 'wss://agri.example/live');
});

test('connects, reports status and dispatches messages by type', () => {
  const { feed, statuses, sock } = setup();
  const seen = [];
  feed.on('stock_changed', (m) => seen.push(['a', m.stock_g])).on('stock_changed', (m) => seen.push(['b', m.stock_g])).on('price_changed', () => seen.push('price'));
  feed.start();
  assert.deepEqual(statuses, ['connecting']);
  sock().open();
  assert.deepEqual(statuses, ['connecting', 'live']);
  sock().receive({ type: 'stock_changed', pool_id: 1, stock_g: 1500000 });
  sock().receive('not json');
  sock().receive({ no_type: true });
  sock().receive({ type: 'unknown' });
  assert.deepEqual(seen, [['a', 1500000], ['b', 1500000]]);
});

test('a throwing handler does not stop the others', () => {
  const { feed, sock } = setup();
  const seen = [];
  const real = console.error;
  console.error = () => {};
  feed.on('x', () => { throw new Error('boom'); }).on('x', () => seen.push('ok'));
  feed.start(); sock().open(); sock().receive({ type: 'x' });
  console.error = real;
  assert.deepEqual(seen, ['ok']);
});

test('reconnects with growing back-off, and starts over after a successful open', () => {
  const { feed, timers, statuses, sock } = setup();
  feed.start(); sock().open();
  const waits = [];
  for (let i = 0; i < 6; i += 1) {
    sock().drop();
    waits.push(timers.at(-1).ms);
    timers.at(-1).fn();   // the wait is over: a new socket is made
  }
  assert.deepEqual(waits, [1000, 2000, 4000, 8000, 15000, 15000]);
  assert.equal(FakeSocket.instances.length, 7);
  sock().open();            // success resets the back-off
  sock().drop();
  assert.equal(timers.at(-1).ms, 1000);
  assert.ok(statuses.includes('reconnecting'));
});

test('after a reconnect it re-fetches first and applies the messages that came meanwhile, in order', async () => {
  const { feed, timers, resyncs, statuses, sock } = setup();
  const seen = [];
  feed.on('stock_changed', (m) => seen.push(m.stock_g));
  feed.start(); sock().open();
  sock().receive({ type: 'stock_changed', stock_g: 1 });
  sock().drop(); timers.at(-1).fn();
  sock().open();                                       // reconnected: resync begins
  sock().receive({ type: 'stock_changed', stock_g: 2 });   // arrives while the page is re-fetching
  sock().receive({ type: 'stock_changed', stock_g: 3 });
  assert.deepEqual(seen, [1], 'nothing is applied before the re-fetch is done');
  await tick(); await tick();
  assert.deepEqual(resyncs, ['start', 'done']);
  assert.deepEqual(seen, [1, 2, 3]);
  assert.equal(statuses.at(-1), 'live');
  sock().receive({ type: 'stock_changed', stock_g: 4 });
  assert.deepEqual(seen, [1, 2, 3, 4]);
});

test('a resync message from the server triggers the same re-fetch', async () => {
  const { feed, resyncs, sock } = setup();
  feed.start(); sock().open();
  sock().receive({ type: 'resync' });
  await tick(); await tick();
  assert.deepEqual(resyncs, ['start', 'done']);
});

test('a failing re-fetch does not block live updates', async () => {
  const { feed, timers, sock } = setup({ onResync: async () => { throw new Error('offline'); } });
  const seen = [];
  feed.on('x', () => seen.push(1));
  feed.start(); sock().open(); sock().drop(); timers.at(-1).fn(); sock().open();
  await tick(); await tick();
  sock().receive({ type: 'x' });
  assert.deepEqual(seen, [1]);
});

test('stop() closes the socket and never reconnects', () => {
  const { feed, timers, sock } = setup();
  feed.start(); sock().open();
  const s = sock();
  feed.stop();
  assert.ok(s.closed);
  s.drop && s.drop();
  assert.equal(timers.length, 0, 'no retry is scheduled after stop');
  assert.equal(FakeSocket.instances.length, 1);
});
