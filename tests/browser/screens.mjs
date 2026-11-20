// Real screens, real server, real database. Usage: node tests/browser/screens.mjs [login|buyer|farmer|manager|all]
// Run through scripts/screens_test.sh (it prepares the database). Screenshots go to tests/browser/out/.
import { startServer, launch, newPage, review, VIEWPORTS } from './lib.mjs';
import { randomUUID } from 'node:crypto';
import { seed, PASS, TEMP } from './fixtures.mjs';

const which = process.argv[2] || 'all';
const { child, port, env } = await startServer();
const base = `http://127.0.0.1:${port}`;
const browser = await launch();
let failed = 0;
const tally = (list) => { failed += list.length; };

async function uiLogin(page, username, password = PASS) {
  await page.goto(`${base}/login.html`);
  await page.waitForSelector('html[data-ready="true"]', { state: 'attached' });
  await page.fill('#username', username);
  await page.fill('#password', password);
  await page.click('button[type=submit]');
}

// ---- login ------------------------------------------------------------------------------------------------------------
async function login(data, vp) {
  const { context, page } = await newPage(browser, base, vp);
  page.allowedStatuses = [401, 404];
  await page.goto(`${base}/login.html`);
  await page.waitForSelector('html[data-ready="true"]', { state: 'attached' });
  tally(await review(page, `login-${vp.label}`, vp));

  // a wrong password: one generic sentence, with an icon, and the password field emptied and focused
  await page.fill('#username', data.buyers[0].username);
  await page.fill('#password', 'not-the-password');
  await page.click('button[type=submit]');
  await page.waitForSelector('#login-problem:not([hidden])');
  tally(await review(page, `login-error-${vp.label}`, vp, { skipGrey: true }));
  const sentence = await page.textContent('#login-problem');
  if (!/username or password is incorrect/.test(sentence)) { console.log(`FAIL  login error sentence is "${sentence}"`); failed += 1; }
  if (/401|INVALID/.test(sentence)) { console.log('FAIL  the login error shows a code'); failed += 1; }

  // a slow server: the button shows Working… and cannot be tapped twice
  await page.route('**/auth/login', async (route) => { await new Promise((r) => setTimeout(r, 1200)); await route.continue(); });
  await page.fill('#password', PASS);
  await page.click('button[type=submit]');
  await page.waitForSelector('button[aria-busy="true"]');
  tally(await review(page, `login-loading-${vp.label}`, vp, { skipGrey: true }));
  await page.unroute('**/auth/login');
  await context.close();

  // the session-ended notice
  const ended = await newPage(browser, base, vp);
  await ended.page.goto(`${base}/login.html?ended=1`);
  await ended.page.waitForSelector('html[data-ready="true"]', { state: 'attached' });
  tally(await review(ended.page, `login-ended-${vp.label}`, vp, { skipGrey: true }));
  await ended.context.close();

  // first login: temporary password, then the forced change, then the role home
  const username = `fresh${vp.label}.${data.unique}`;
  await data.managerApi.post('/users', { name: 'Shivappa Naik', username, type: 'BUYER', temporary_password: TEMP });
  const first = await newPage(browser, base, vp);
  first.page.allowedStatuses = [401, 403, 404];
  await uiLogin(first.page, username, TEMP);
  await first.page.waitForSelector('#new-password');
  tally(await review(first.page, `login-password-${vp.label}`, vp));
  await first.page.fill('#new-password', 'short');
  await first.page.fill('#repeat-password', 'short');
  await first.page.click('button[type=submit]');
  await first.page.waitForSelector('#new-password-error:not([hidden])');
  await first.page.fill('#new-password', PASS);
  await first.page.fill('#repeat-password', PASS + 'x');
  await first.page.click('button[type=submit]');
  await first.page.waitForSelector('#repeat-password-error:not([hidden])');
  tally(await review(first.page, `login-password-error-${vp.label}`, vp, { skipGrey: true }));
  await first.page.fill('#repeat-password', PASS);
  await first.page.click('button[type=submit]');
  await first.page.waitForURL(/\/buyer\/pools\.html/, { timeout: 10000 });
  await first.context.close();
}

// ---- buyer ------------------------------------------------------------------------------------------------------------
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
async function until(read, ok, what, ms = 8000) {
  const end = Date.now() + ms;
  while (Date.now() < end) { const v = await read(); if (ok(v)) return v; await sleep(100); }
  console.log(`FAIL  timed out waiting for ${what}`); failed += 1; return null;
}
const check = (cond, what) => { if (!cond) { console.log(`FAIL  ${what}`); failed += 1; } };
const ready = (page) => page.waitForSelector('html[data-ready="true"]', { state: 'attached', timeout: 15000 });

async function buyer(data, vp) {
  const user = data.buyers[1];   // Gangadhar Agro Mart: no purchases yet
  const { context, page } = await newPage(browser, base, vp);
  const stem = (n) => `${n}-${vp.label}`;
  await uiLogin(page, user.username);
  await page.waitForURL(/\/buyer\/pools\.html/);
  await ready(page);
  await page.waitForSelector('.pool-card');

  // the list: cards with photo, price, available stock; the sold-out pool greyed with a disabled button
  tally(await review(page, stem('buyer-pools'), vp, { allowManyPrimary: true, shots: { card: '.pool-card' } }));
  const maize = page.locator('.pool-card', { hasText: 'Pioneer 30V92' }).first();
  check((await maize.textContent()).includes('Out of stock'), 'the sold-out pool shows Out of stock');
  check((await maize.locator('a.btn').getAttribute('aria-disabled')) === 'true', 'the sold-out pool button is disabled');
  check(!(await page.textContent('main')).match(/Basavaraj|Renukamma|Mallikarjun/), 'no farmer name on the buyer list');

  // error and loading states of the list
  page.allowedStatuses = [500];
  await page.route('**/pools', (route) => route.request().method() === 'GET' ? route.fulfill({ status: 500, contentType: 'application/json', body: '{"error":"INTERNAL"}' }) : route.continue());
  await page.reload(); await ready(page); await page.waitForSelector('.notice--bad');
  tally(await review(page, stem('buyer-pools-error'), vp, { skipGrey: true }));
  check(!/500|INTERNAL/.test(await page.textContent('main')), 'the list error shows no code');
  await page.unroute('**/pools');
  await page.route('**/pools', async (route) => { await sleep(1500); await route.continue(); });
  await page.reload(); await page.waitForSelector('.skeleton');
  tally(await review(page, stem('buyer-pools-loading'), vp, { skipGrey: true, allowManyPrimary: true }));
  await page.unroute('**/pools'); await ready(page);

  // the unit toggle changes every quantity and is remembered
  const stockBefore = await page.locator('.pool-card', { hasText: 'Sona Masuri' }).locator('.fig--right .fig__value').textContent();
  await page.click('.toggle__opt[data-unit="kg"]');
  const stockKg = await page.locator('.pool-card', { hasText: 'Sona Masuri' }).locator('.fig--right .fig__value').textContent();
  check(stockBefore !== stockKg && /,/.test(stockKg), `kg toggle changed ${stockBefore} to ${stockKg}`);
  await page.click('.toggle__opt[data-unit="quintal"]');

  // the pool screen
  await page.locator('.pool-card', { hasText: 'Sona Masuri' }).locator('a.btn').click();
  await page.waitForURL(/\/buyer\/pool\.html\?id=/); await ready(page); await page.waitForSelector('#quantity');
  const paddyUrl = page.url();
  tally(await review(page, stem('buyer-pool'), vp, { shots: { buybox: '.card--key', limits: 'section:has(.table)' } }));
  { const main = await page.textContent('main'); check(main.includes('2 farmers (names are private)'), `farmer count, no names (page says: ${main.replace(/\s+/g, ' ').slice(main.indexOf('Contributed'), main.indexOf('Contributed') + 60)})`); }
  await page.fill('#quantity', '0.5');
  await page.waitForSelector('#quantity-error:not([hidden])');
  check(/minimum order for this pool is 1\.0 quintal/.test(await page.textContent('#quantity-error')), 'below-minimum message appears while typing');
  await page.fill('#quantity', '5.123456');
  check(/at most 5 decimals/.test(await page.textContent('#quantity-error')), 'too-precise message');
  tally(await review(page, stem('buyer-pool-invalid'), vp, { skipGrey: true, shots: { buybox: '.card--key' } }));
  await page.fill('#quantity', '5');
  check((await page.textContent('.total-box__value')).includes('12,500.00'), 'live total for 5 quintal at 2,500');

  // the happy path: review, confirm once (double tap protected), receipt, purchases
  await page.click('button:has-text("Review order")');
  await page.waitForSelector('.modal');
  tally(await review(page, stem('buyer-confirm'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  let posts = 0;
  await page.route('**/sales', (route) => { posts += 1; return route.continue(); });
  await page.dblclick('.modal button.btn--primary');
  await page.waitForSelector('.modal :text("Purchase complete")');
  check(posts === 1, `a double tap sent ${posts} purchase requests`);
  tally(await review(page, stem('buyer-receipt'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  check(/R-\d{4}/.test(await page.textContent('.modal')) && (await page.textContent('.modal')).includes('₹12,500.00'), 'the receipt shows the number and total');
  check(!/Basavaraj|payout/i.test(await page.textContent('.modal')), 'the buyer receipt shows no farmers or payouts');
  await page.unroute('**/sales');
  await page.click('.modal button:has-text("View my purchases")');
  await page.waitForURL(/purchases\.html/); await ready(page); await page.waitForSelector('.table');
  tally(await review(page, stem('buyer-purchases'), vp));

  // empty and error states of My purchases
  await page.route('**/me/purchases', (route) => route.fulfill({ status: 200, contentType: 'application/json', body: '[]' }));
  await page.reload(); await ready(page); await page.waitForSelector('.state');
  tally(await review(page, stem('buyer-purchases-empty'), vp, { skipGrey: true }));
  await page.unroute('**/me/purchases');
  await page.route('**/me/purchases', (route) => route.fulfill({ status: 500, contentType: 'application/json', body: '{"error":"INTERNAL"}' }));
  await page.reload(); await ready(page); await page.waitForSelector('.notice--bad');
  tally(await review(page, stem('buyer-purchases-error'), vp, { skipGrey: true }));
  await page.unroute('**/me/purchases'); page.allowedStatuses = [];

  // refusals, each on its own pool so the shared data stays intact
  const extra = async (price, grams, variety) => data.make({ commodity: 'Maize', variety, range: 1, packaging: '50 kg HDPE bags', storage: 'Hubballi APMC yard', min: 100000, price }, [[210, 170, 90]], [[0, grams]]);
  const other = new (await import('./fixtures.mjs')).Api(base); await other.login(data.buyers[0].username, PASS);
  const open = async (poolId, quantity) => {
    await page.goto(`${base}/buyer/pool.html?id=${poolId}`); await ready(page); await page.waitForSelector('#quantity');
    await page.fill('#quantity', quantity); await page.click('button:has-text("Review order")'); await page.waitForSelector('.modal');
  };

  page.allowedStatuses = [409];   // the refusals below are expected 409 answers
  // someone else buys first: only part of the stock is left
  const shortPool = await extra(3000, 1000000, `Short ${vp.label} ${Date.now() % 1000}`);
  await open(shortPool, '8');
  await other.post(`/pools/${shortPool}/sales`, { request_id: randomUUID(), quantity_g: 500000, confirmed_price_paise_per_kg: 3000 });
  await page.dblclick('.modal button.btn--primary');
  await page.waitForSelector('.modal :text("is available right now")');
  tally(await review(page, stem('buyer-some-left'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  check(!/INSUFFICIENT|409/.test(await page.textContent('.modal')), 'the some-stock-left message shows no code');
  await page.click('.modal button:has-text("Buy remaining")');
  await page.waitForSelector('.modal :text("5.0 quintal")');
  await page.click('.modal button.btn--primary'); await page.waitForSelector('.modal :text("Purchase complete")');

  // the price changes while the confirmation is open
  const pricePool = await extra(2500, 1000000, `Price ${vp.label} ${Date.now() % 1000}`);
  await open(pricePool, '2');
  await data.managerApi.patch(`/pools/${pricePool}`, { price_paise_per_kg: 2400 });
  await page.click('.modal button.btn--primary');
  await page.waitForSelector('.modal :text("The price has changed")');
  tally(await review(page, stem('buyer-price-changed'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  check((await page.textContent('.modal')).includes('From ₹2,500.00 to ₹2,400.00 per quintal. Your total is now ₹4,800.00 for 2.0 quintal.'), 'the price-changed sentence');
  await page.click('.modal button:has-text("Review and confirm again")');
  await page.waitForSelector('.modal :text("₹2,400.00")');
  await page.click('.modal button.btn--primary'); await page.waitForSelector('.modal :text("Purchase complete")');

  // busy: the same request id is sent again by "Try again"
  const busyPool = await extra(2500, 1000000, `Busy ${vp.label} ${Date.now() % 1000}`);
  await open(busyPool, '1');
  const ids = []; let first = true;
  page.allowedStatuses = [409, 503];
  await page.route('**/sales', async (route) => {
    ids.push(JSON.parse(route.request().postData()).request_id);
    if (first) { first = false; await route.fulfill({ status: 503, contentType: 'application/json', headers: { 'Retry-After': '1' }, body: '{"status":"REJECTED","reason":"BUSY"}' }); } else await route.continue();
  });
  await page.click('.modal button.btn--primary');
  await page.waitForSelector('.modal :text("Many buyers are ordering right now")');
  tally(await review(page, stem('buyer-busy'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.click('.modal button:has-text("Try again")');
  await page.waitForSelector('.modal :text("Purchase complete")');
  check(ids.length === 2 && ids[0] === ids[1], `Try again reused the request id (${ids.length} requests, same id: ${ids[0] === ids[1]})`);
  await page.unroute('**/sales'); page.allowedStatuses = [];

  // out of stock (simulated: another buyer took the last of it)
  const goneUrl = `${base}/buyer/pool.html?id=${data.pools.paddy}`;
  await page.goto(goneUrl); await ready(page); await page.waitForSelector('#quantity');
  page.allowedStatuses = [409];
  await page.route('**/sales', (route) => route.fulfill({ status: 409, contentType: 'application/json', body: '{"status":"REJECTED","reason":"OUT_OF_STOCK"}' }));
  await page.fill('#quantity', '1'); await page.click('button:has-text("Review order")'); await page.waitForSelector('.modal');
  await page.click('.modal button.btn--primary'); await page.waitForSelector('.modal :text("This pool is out of stock")');
  tally(await review(page, stem('buyer-out-of-stock'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.unroute('**/sales'); page.allowedStatuses = [];

  // live: someone else buys, this screen updates without a reload
  await page.goto(`${base}/buyer/pools.html`); await ready(page); await page.waitForSelector('.pool-card');
  const gn = page.locator('.pool-card', { hasText: 'JL-24' }).locator('.fig--right .fig__value');
  const before = await gn.textContent();
  await other.post(`/pools/${data.pools.groundnut}/sales`, { request_id: randomUUID(), quantity_g: 100000, confirmed_price_paise_per_kg: 6783 });
  const after = await until(() => gn.textContent(), (v) => v !== before, 'the live stock change');
  check(after !== null && after !== before, `live update changed ${before} to ${after} without a reload`);
  await context.close();
}

const scenarios = { login, buyer };
try {
  const data = await seed(base, env);
  for (const [name, fn] of Object.entries(scenarios)) {
    if (which !== 'all' && which !== name) continue;
    for (const vp of VIEWPORTS) await fn(data, vp);
  }
} finally {
  await browser.close();
  child.kill();
}
console.log(failed ? `\n${failed} problem(s). Screenshots are in tests/browser/out/` : '\nAll screen checks passed. Screenshots are in tests/browser/out/');
process.exit(failed ? 1 : 0);
