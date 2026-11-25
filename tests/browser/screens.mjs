// Real screens, real server, real database. Usage: node tests/browser/screens.mjs [login|buyer|farmer|manager|all]
// Run through scripts/screens_test.sh (it prepares the database). Screenshots go to tests/browser/out/.
import { startServer, launch, newPage, review, VIEWPORTS } from './lib.mjs';
import { randomUUID } from 'node:crypto';
import { seed, PASS, TEMP, png, Api } from './fixtures.mjs';

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

// ---- farmer -----------------------------------------------------------------------------------------------------------
async function farmer(data, vp) {
  const user = data.farmers[0];   // Basavaraj Hiremath: deposits in the paddy and groundnut pools
  const { context, page } = await newPage(browser, base, vp);
  const stem = (n) => `${n}-${vp.label}`;
  await uiLogin(page, user.username);
  await page.waitForURL(/\/farmer\/pools\.html/); await ready(page); await page.waitForSelector('.pool-card');
  tally(await review(page, stem('farmer-pools'), vp, { shots: { card: '.pool-card', activity: '#activity' } }));
  const text = await page.textContent('main');
  check(!/Renukamma|Mallikarjun|Mahantesh|Gangadhar/.test(text), 'a farmer sees no other farmer or buyer by name');
  check(/Latest activity/.test(text) && /Sale from P-/.test(text), 'the latest activity shows the sale');
  check((await page.locator('.pool-card').count()) >= 2, 'a card for each pool the farmer deposited into');

  // error, loading and empty
  page.allowedStatuses = [500];
  await page.route('**/me/pools', (route) => route.fulfill({ status: 500, contentType: 'application/json', body: '{"error":"INTERNAL"}' }));
  await page.reload(); await ready(page); await page.waitForSelector('.notice--bad');
  tally(await review(page, stem('farmer-pools-error'), vp, { skipGrey: true }));
  await page.unroute('**/me/pools'); page.allowedStatuses = [];
  await page.route('**/me/pools', async (route) => { await sleep(1500); await route.continue(); });
  await page.reload(); await page.waitForSelector('.skeleton');
  tally(await review(page, stem('farmer-pools-loading'), vp, { skipGrey: true }));
  await page.unroute('**/me/pools'); await ready(page);

  const fresh = `fresh.farmer${vp.label}.${data.unique}`;
  await data.managerApi.post('/users', { name: 'Renukamma Naik', username: fresh, type: 'FARMER', temporary_password: TEMP });
  const newcomer = await newPage(browser, base, vp);
  await newcomer.page.goto(`${base}/login.html`);
  await newcomer.page.fill('#username', fresh); await newcomer.page.fill('#password', TEMP); await newcomer.page.click('button[type=submit]');
  await newcomer.page.waitForSelector('#new-password');
  await newcomer.page.fill('#new-password', PASS); await newcomer.page.fill('#repeat-password', PASS); await newcomer.page.click('button[type=submit]');
  await newcomer.page.waitForURL(/\/farmer\/pools\.html/); await ready(newcomer.page); await newcomer.page.waitForSelector('.state');
  tally(await review(newcomer.page, stem('farmer-pools-empty'), vp, { skipGrey: true }));
  await newcomer.context.close();

  // live: the Pool Manager records a deposit for this farmer; the card changes by itself
  const grain = page.locator('.pool-card', { hasText: 'Sona Masuri' }).locator('.fig', { hasText: 'My grain left' }).locator('.fig__value');
  const before = await grain.textContent();
  await data.managerApi.post(`/pools/${data.pools.paddy}/contributions`, { seller_id: user.id, quantity_g: 300000 });
  const after = await until(() => grain.textContent(), (v) => v !== before, 'the live deposit');
  check(after !== null && after !== before, `live update changed my grain left from ${before} to ${after}`);

  // the statement
  await page.locator('.pool-card', { hasText: 'Sona Masuri' }).locator('a.btn').click();
  await page.waitForURL(/statement\.html\?pool=/); await ready(page); await page.waitForSelector('.tile');
  tally(await review(page, stem('farmer-statement'), vp, { shots: { tiles: '.grid.cols-4', table: '.card--table' } }));
  const st = await page.textContent('main');
  check(/These figures match the pool's records/.test(st) && /I deposited grain/.test(st) && /Sold .* at .* per quintal\. My part: /.test(st), 'the statement lists deposits and sales and says the figures match');
  check(/Bought by Mahantesh Traders · Receipt R-\d{4} · Seal [0-9A-F]{4} [0-9A-F]{4}/.test(st) && /Statement for Basavaraj Hiremath/.test(st), 'each sale shows the buyer, the receipt and the record seal; the title names the farmer');
  check(!/Renukamma|Mallikarjun/.test(st), 'the statement shows only this farmer (no other farmer\'s name)');
  const unit0 = await page.locator('.tile .key-number').nth(1).textContent();
  await page.click('.toggle__opt[data-unit="kg"]');
  check((await page.locator('.tile .key-number').nth(1).textContent()) !== unit0, 'the unit toggle changes the statement');
  await page.click('.toggle__opt[data-unit="quintal"]');

  page.allowedStatuses = [500];
  await page.route('**/farmers/*/statement*', (route) => route.fulfill({ status: 500, contentType: 'application/json', body: '{"error":"INTERNAL"}' }));
  await page.reload(); await ready(page); await page.waitForSelector('.notice--bad');
  tally(await review(page, stem('farmer-statement-error'), vp, { skipGrey: true }));
  await page.unroute('**/farmers/*/statement*'); page.allowedStatuses = [];

  // "My statement" in the menu opens the first pool
  await page.goto(`${base}/farmer/statement.html`); await ready(page); await page.waitForSelector('.tile');
  check(await page.locator('.table').count() === 1, 'My statement without a pool opens one');
  await context.close();
}

// ---- pool manager -----------------------------------------------------------------------------------------------------
async function manager(data, vp) {
  const { context, page } = await newPage(browser, base, vp);
  const stem = (n) => `${n}-${vp.label}`;
  await uiLogin(page, data.manager.username);
  await page.waitForURL(/\/manager\/pools\.html/); await ready(page); await page.waitForSelector('.table--rows tbody tr');

  // the pools table
  tally(await review(page, stem('manager-pools'), vp, { shots: { table: '.card--table' } }));
  const listText = await page.textContent('main');
  check(/Draft/.test(listText) && /Out of stock/.test(listText) && /Records verified/.test(listText), 'the table shows draft, out of stock and reconciled');
  await page.selectOption('select[aria-label="Status"]', 'draft');
  check((await page.locator('.table--rows tbody tr').count()) >= 1 && !(await page.textContent('main')).includes('Sona Masuri'), 'the status filter shows only drafts');
  await page.selectOption('select[aria-label="Status"]', '');
  page.allowedStatuses = [500];
  await page.route('**/pools', (route) => route.request().method() === 'GET' ? route.fulfill({ status: 500, contentType: 'application/json', body: '{"error":"INTERNAL"}' }) : route.continue());
  await page.reload(); await ready(page); await page.waitForSelector('.notice--bad');
  tally(await review(page, stem('manager-pools-error'), vp, { skipGrey: true }));
  await page.unroute('**/pools'); page.allowedStatuses = [];
  await page.route('**/pools', async (route) => { await sleep(1500); await route.continue(); });
  await page.reload(); await page.waitForSelector('.skeleton');
  tally(await review(page, stem('manager-pools-loading'), vp, { skipGrey: true }));
  await page.unroute('**/pools'); await ready(page);
  await page.route('**/pools', (route) => route.request().method() === 'GET' ? route.fulfill({ status: 200, contentType: 'application/json', body: '[]' }) : route.continue());
  await page.reload(); await ready(page); await page.waitForSelector('.state');
  tally(await review(page, stem('manager-pools-empty'), vp, { skipGrey: true, allowManyPrimary: true }));
  await page.unroute('**/pools'); await page.reload(); await ready(page); await page.waitForSelector('.table--rows tbody tr');

  // the pool screen
  await page.locator('.table--rows tbody tr', { hasText: 'Sona Masuri' }).first().click();
  await page.waitForURL(/\/manager\/pool\.html\?id=/); await ready(page); await page.waitForSelector('.ledger tbody tr');
  const poolUrl = page.url();
  tally(await review(page, stem('manager-pool'), vp, { allowManyPrimary: true, shots: { cards: '.grid.cols-3', photos: '.grid--unequal', ledgers: '.ledgers' } }));
  const ptext = await page.textContent('main');
  check(/Basavaraj Hiremath/.test(ptext) && /Sale total/.test(ptext) && /Paid to /.test(ptext) && /Deposit/.test(ptext), 'farmers, deposits, sale total and payouts are shown to the Manager');
  check(await page.locator('.photo').count() >= 3, 'photo slots: two photos and the add slot');

  // change price: a decimal is refused, whole rupees are saved, the change appears as a different row
  await page.click('button:has-text("Change price")');
  await page.waitForSelector('.modal #new-price');
  await page.fill('#new-price', '2500.50');
  await page.click('.modal button.btn--primary');
  await page.waitForSelector('#new-price-error:not([hidden])');
  check(/whole rupees/.test(await page.textContent('#new-price-error')), 'a decimal price is refused in plain words');
  tally(await review(page, stem('manager-change-price'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.fill('#new-price', '2,600');
  await page.click('.modal button.btn--primary');
  await page.waitForSelector('.modal', { state: 'detached' });
  await until(() => page.textContent('main'), (v) => v.includes('₹2,600.00'), 'the new price on the screen');
  await until(() => page.locator('.ledger__change').count(), (c) => c >= 1, 'the price change row in the stock ledger');
  check(/Price changed ₹2,500 to ₹2,600/.test(await page.textContent('.ledgers')), 'the price change is shown as a different row');
  await data.managerApi.patch(`/pools/${data.pools.paddy}`, { price_paise_per_kg: 2500 });   // put it back for the other roles

  // verify integrity
  await page.click('button:has-text("Check records")');
  await page.waitForSelector('text=/All \\d+ records match/');
  tally(await review(page, stem('manager-verified'), vp, { allowManyPrimary: true, skipGrey: true, shots: { cards: '.grid.cols-3' } }));

  // close and reopen on its own pool
  const temp = await data.make({ commodity: 'Maize', variety: `Close ${vp.label} ${Date.now() % 1000}`, range: 1, packaging: '50 kg HDPE bags', storage: 'Hubballi APMC yard', min: 100000, price: 2000 }, [[210, 170, 90]], [[0, 500000]]);
  await page.goto(`${base}/manager/pool.html?id=${temp}`); await ready(page); await page.waitForSelector('button:has-text("Close pool")');
  await page.click('button:has-text("Close pool")'); await page.waitForSelector('.modal');
  tally(await review(page, stem('manager-close'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.click('.modal button.btn--primary'); await page.waitForSelector('button:has-text("Reopen pool")');
  check(/Closed/.test(await page.textContent('main')), 'the pool shows Closed');
  await page.click('button:has-text("Reopen pool")'); await page.click('.modal button.btn--primary'); await page.waitForSelector('button:has-text("Close pool")');

  // photos through the real file input: a good one, a wrong type, one that is too large
  const draftPool = data.pools.tur;
  await page.goto(`${base}/manager/pool.html?id=${draftPool}`); await ready(page); await page.waitForSelector('#photo-input', { state: 'attached' });
  await page.setInputFiles('#photo-input', { name: 'notes.pdf', mimeType: 'application/pdf', buffer: Buffer.from('%PDF-1.4') });
  await page.waitForSelector('.notice--bad:not([hidden])');
  check(/Only JPEG and PNG/.test(await page.textContent('main')), 'a PDF is refused in plain words');
  await page.setInputFiles('#photo-input', { name: 'big.png', mimeType: 'image/png', buffer: Buffer.alloc(6 * 1024 * 1024, 1) });
  await page.waitForFunction ? null : null;
  await until(() => page.textContent('main'), (v) => /too large/.test(v), 'the too-large message');
  await page.setInputFiles('#photo-input', { name: 'lot.png', mimeType: 'image/png', buffer: png(400, 300, [200, 160, 60], 99) });
  await until(() => page.locator('.photo img').count(), (c) => c >= 1, 'the uploaded photo (a real browser upload)');
  check(!/needs a photo/.test(await page.textContent('main')), 'after a photo the draft no longer needs one');
  tally(await review(page, stem('manager-photo-uploaded'), vp, { allowManyPrimary: true, skipGrey: true, shots: { photos: '.grid--unequal' } }));

  // live: a buyer buys, the stock and the ledgers change by themselves, the new rows are highlighted
  await page.goto(`${base}/manager/pool.html?id=${data.pools.groundnut}`); await ready(page); await page.waitForSelector('.ledger tbody tr');
  const buyerApi = new Api(base); await buyerApi.login(data.buyers[0].username, PASS);
  const stockBefore = await page.locator('.key-number--xl').textContent();
  await buyerApi.post(`/pools/${data.pools.groundnut}/sales`, { request_id: randomUUID(), quantity_g: 200000, confirmed_price_paise_per_kg: 6783 });
  await until(() => page.locator('.key-number--xl').textContent(), (v) => v !== stockBefore, 'the live stock change');
  await until(() => page.locator('.ledger tr.is-new').count(), (c) => c >= 3, 'new ledger rows (stock, ownership, money)');
  check(/Sale total/.test(await page.textContent('.ledgers')), 'the money ledger gained the sale total live');
  tally(await review(page, stem('manager-live-rows'), vp, { allowManyPrimary: true, skipGrey: true, shots: { ledgers: '.ledgers' } }));

  // create pool, end to end
  // market prices: nothing saved yet, then "Update market prices" reads the saved sample file (offline mode of the test server)
  if (vp.label === '1200') {   // the sample prices are loaded once; the phone run starts with them already saved
    await page.goto(`${base}/manager/create.html`); await ready(page); await page.waitForSelector('#market-card');
    await page.waitForSelector('#market-card :text("No market prices saved yet")');
    tally(await review(page, stem('manager-create-no-prices'), vp, { allowManyPrimary: true, skipGrey: true }));
  }
  await page.goto(`${base}/manager/pools.html`); await ready(page); await page.waitForSelector('button:has-text("Update market prices")');
  await page.click('button:has-text("Update market prices")');
  page.allowedStatuses = [429];   // a second press within a minute is refused on purpose (the service is shared)
  await page.waitForSelector('.notice:has-text("updated")');
  check(/\d+ quotes saved|a moment ago/.test(await page.textContent('main')), 'the update says how many quotes were saved, or that it just ran');
  page.allowedStatuses = [];
  await page.goto(`${base}/manager/create.html`); await ready(page); await page.waitForSelector('#variety');
  check((await page.locator('#variety option').allTextContents()).includes('Sona Mahsuri') && (await page.locator('#variety option').allTextContents()).includes('Other'), 'the variety is chosen from a list, with Other');
  await page.selectOption('#variety', 'Sona Mahsuri');
  await page.waitForSelector('#market-card table');
  const marketText = await page.textContent('#market-card');
  check(/₹[\d,]+ per quintal/.test(marketText) && /Sona Mahsuri/.test(marketText) && /Source: Agmarknet via data\.gov\.in/.test(marketText) && /25 Nov/.test(marketText), 'the market card shows the median price, the variety, the date and the source');
  check(/Lowest/.test(marketText) && /Highest/.test(marketText) && /Most common/.test(marketText) && /Market grade/.test(marketText), 'the table shows lowest, highest, most common price and the market grade');
  check(await page.inputValue('#price') === '', 'the price field stays blank: the Pool Manager decides');
  tally(await review(page, stem('manager-create-prices'), vp, { allowManyPrimary: true, skipGrey: true, shots: { prices: '#market-card' } }));
  await page.selectOption('#variety', '__other'); await page.waitForSelector('#variety-other');
  await page.selectOption('#commodity', { label: 'Tur (Arhar)' });
  check((await page.locator('#variety option').allTextContents()).includes('Arhar (Whole)'), 'changing the commodity changes the variety list');
  await page.reload(); await ready(page); await page.waitForSelector('#variety');
  tally(await review(page, stem('manager-create'), vp, { allowManyPrimary: true, shots: { form: '.detail-grid' } }));
  check(await page.locator('button:has-text("Open for sale")').isDisabled(), 'Open for sale waits for a photo');
  await page.selectOption('#commodity', { label: 'Maize' });
  await page.selectOption('#variety', '__other'); await page.fill('#variety-other', `Hybrid ${vp.label} ${Date.now() % 1000}`);
  await page.click('.toggle__opt:has-text("2")');
  await page.fill('#price', '2,090.50'); await page.fill('#min-order', '2'); await page.fill('#packaging', '50 kg HDPE bags'); await page.fill('#storage', 'Hubballi APMC yard');
  await page.click('button:has-text("Save as draft")');
  await page.waitForSelector('#market-error:not([hidden])');
  check(/required/.test(await page.textContent('#market-error')), 'the market is required');
  await page.fill('#market', 'Hubballi APMC');
  await page.waitForSelector('#price-error:not([hidden])');
  check(/whole rupees/.test(await page.textContent('#price-error')), 'create pool refuses a decimal price');
  tally(await review(page, stem('manager-create-errors'), vp, { allowManyPrimary: true, skipGrey: true }));
  await page.fill('#price', '2,090');
  await page.setInputFiles('#photo-pick', { name: 'lot.png', mimeType: 'image/png', buffer: png(400, 300, [214, 178, 80], 7) });
  await until(() => page.locator('button:has-text("Open for sale")').isEnabled(), (v) => v === true, 'Open for sale after the first photo');
  check(await page.locator('#variety').isDisabled(), 'the saved draft fields are locked');
  tally(await review(page, stem('manager-create-photo'), vp, { allowManyPrimary: true, skipGrey: true, shots: { form: '.detail-grid' } }));
  await page.click('button:has-text("Open for sale")');
  await page.waitForURL(/\/manager\/pool\.html\?id=/); await ready(page); await page.waitForSelector('.tag--ok');
  check(/Open/.test(await page.textContent('main')), 'the new pool is open');

  // the three forms
  await page.goto(`${base}/manager/accounts.html`); await ready(page); await page.waitForSelector('#form-deposit');
  tally(await review(page, stem('manager-accounts'), vp, { allowManyPrimary: true, shots: { forms: '.grid--forms' } }));
  await page.selectOption('#dep-pool', { index: 0 });
  await page.fill('#dep-qty', '2');
  await page.waitForSelector('#dep-stock strong');
  check((await page.textContent('#dep-stock')).includes('Pool stock') && (await page.locator('#dep-stock .icon').count()) === 1, 'the after-saving preview shows stock before and after');
  await page.click('button:has-text("Save deposit")');
  await page.waitForSelector('#form-deposit .notice--bad:not([hidden])');
  check(/Tick the box/.test(await page.textContent('#form-deposit')), 'a deposit needs the tested confirmation');
  await page.check('#dep-tested'); await page.click('button:has-text("Save deposit")');
  await page.waitForSelector('#form-deposit .notice--grey:not([hidden])');
  check(/Deposit saved/.test(await page.textContent('#form-deposit')), 'the deposit is saved');
  await page.fill('#dep-qty', '0'); await page.check('#dep-tested'); await page.click('button:has-text("Save deposit")');
  await page.waitForSelector('#dep-qty-error:not([hidden])');


  await page.selectOption('#sale-pool', await page.locator('#sale-pool option', { hasText: 'JL-24' }).getAttribute('value'));
  await page.fill('#sale-qty', '1');
  await page.waitForSelector('#sale-total');
  await page.click('button:has-text("Review order")');
  await page.waitForSelector('.modal');
  await page.click('.modal button.btn--primary');
  await page.waitForSelector('.modal :text("Purchase complete")');
  check(/Paid out|Taken from share/.test(await page.textContent('.modal')) || (await page.locator('.modal table').count()) === 1, 'the Manager sees the split between farmers on the receipt');
  tally(await review(page, stem('manager-sale-receipt'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.click('.modal button:has-text("Close")');

  // farmer statement for any farmer; a pool the farmer has no share in says so
  await page.goto(`${base}/manager/statement.html?farmer=${data.farmers[0].id}&pool=${data.pools.paddy}`); await ready(page); await page.waitForSelector('.tile');
  tally(await review(page, stem('manager-statement'), vp, { shots: { tiles: '.grid.cols-4' } }));
  page.allowedStatuses = [404];   // a farmer with no share is an expected 404
  await page.selectOption('#st-farmer', String(data.farmers[2].id));
  await page.selectOption('#st-pool', String(data.pools.paddy));
  await page.waitForSelector('.state');
  check(/no share in that pool/.test(await page.textContent('main')), 'a farmer with no share in the pool gets a plain sentence');
  tally(await review(page, stem('manager-statement-empty'), vp, { skipGrey: true }));
  page.allowedStatuses = [];
  await context.close();
}

// ---- sign-up, pending accounts and the People page ------------------------------------------------------------------------
async function people(data, vp) {
  const stem = (n) => `${n}-${vp.label}`;
  try { await data.managerApi.post('/market-prices/refresh', {}); } catch { /* already updated a moment ago */ }
  const digits = (Date.now() + (vp.label === '390' ? 7 : 0)).toString().slice(-8);
  const farmerPhone = `98${digits.slice(0, 8)}`, buyerPhone = `97${digits.slice(0, 8)}`;
  // the login page offers sign-up
  const anon = await newPage(browser, base, vp);
  anon.page.allowedStatuses = [409];
  await anon.page.goto(`${base}/login.html`); await ready(anon.page);
  check(/New here\?/.test(await anon.page.textContent('main')), 'the login page offers sign up');
  await anon.page.click('a:has-text("Sign up")');
  await anon.page.waitForURL(/signup\.html/); await ready(anon.page); await anon.page.waitForSelector('#phone');
  tally(await review(anon.page, stem('signup'), vp, { shots: { form: '.auth-card' } }));
  check((await anon.page.locator('#doc option').allTextContents()).join('|') === 'APMC farmer registration|Land record (RTC)|Aadhaar', 'a farmer chooses from farmer documents');
  await anon.page.click('.toggle__opt:has-text("Buyer")');
  check((await anon.page.locator('#doc option').allTextContents()).join('|') === 'APMC trade licence|GST registration|PAN', 'a buyer chooses from trade documents');
  await anon.page.click('.toggle__opt:has-text("Farmer")');
  await anon.page.click('button[type=submit]');
  await anon.page.waitForSelector('#phone-error:not([hidden])');
  tally(await review(anon.page, stem('signup-errors'), vp, { skipGrey: true }));
  check(/10-digit mobile number/.test(await anon.page.textContent('#phone-error')) && /full name/.test(await anon.page.textContent('#name-error')), 'sign-up errors are plain sentences');
  await anon.page.fill('#name', 'Shankar Gouda'); await anon.page.fill('#phone', data.farmers[0].username === farmerPhone ? '9000000009' : farmerPhone);
  await anon.page.selectOption('#doc', 'LAND_RECORD'); await anon.page.fill('#last4', '4821');
  await anon.page.fill('#password', PASS); await anon.page.fill('#repeat-password', PASS);
  await anon.page.click('button[type=submit]');
  await anon.page.waitForURL(/farmer\/pools\.html/, { timeout: 15000 }); await ready(anon.page);
  check(/Waiting for the Pool Manager to verify you\./.test(await anon.page.textContent('body')), 'a new farmer sees that they are waiting to be verified');
  check(/Farmer · Shankar Gouda · F-\d{4}/.test(await anon.page.textContent('.topbar')), 'the top bar shows the generated ID');
  tally(await review(anon.page, stem('signup-pending-farmer'), vp, { skipGrey: true }));
  // a second sign-up with the same mobile number is refused in plain words
  const dup = await newPage(browser, base, vp); dup.page.allowedStatuses = [409];
  await dup.page.goto(`${base}/signup.html`); await ready(dup.page);
  await dup.page.fill('#name', 'Someone Else'); await dup.page.fill('#phone', farmerPhone); await dup.page.fill('#last4', '1111');
  await dup.page.fill('#password', PASS); await dup.page.fill('#repeat-password', PASS); await dup.page.click('button[type=submit]');
  await dup.page.waitForSelector('#phone-error:not([hidden])');
  check(/already registered/.test(await dup.page.textContent('#phone-error')), 'a duplicate mobile number is refused');
  await dup.context.close();
  await anon.context.close();

  // a pending buyer can look at pools but the purchase is refused in plain words
  const bb = await newPage(browser, base, vp); bb.page.allowedStatuses = [409];
  await bb.page.goto(`${base}/signup.html`); await ready(bb.page);
  await bb.page.click('.toggle__opt:has-text("Buyer")');
  await bb.page.fill('#name', 'Sunita Traders'); await bb.page.fill('#phone', buyerPhone); await bb.page.selectOption('#doc', 'GST'); await bb.page.fill('#last4', '1Z5K');
  await bb.page.fill('#password', PASS); await bb.page.fill('#repeat-password', PASS); await bb.page.click('button[type=submit]');
  await bb.page.waitForURL(/buyer\/pools\.html/, { timeout: 15000 }); await ready(bb.page); await bb.page.waitForSelector('.pool-card');
  await bb.page.goto(`${base}/buyer/pool.html?id=${data.pools.paddy}`); await ready(bb.page); await bb.page.waitForSelector('#quantity');
  await bb.page.fill('#quantity', '1'); await bb.page.click('button:has-text("Review order")'); await bb.page.waitForSelector('.modal');
  await bb.page.click('.modal button.btn--primary');
  await bb.page.waitForSelector('.modal :text("Your account is waiting to be verified.")');
  check(!/BUYER_NOT_APPROVED|409/.test(await bb.page.textContent('.modal')), 'the refusal shows no code');
  tally(await review(bb.page, stem('signup-pending-buyer-refused'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await bb.context.close();

  // the Pool Manager's People page
  const { context, page } = await newPage(browser, base, vp);
  page.allowedStatuses = [409];
  await uiLogin(page, data.manager.username);
  await page.waitForURL(/manager\/pools\.html/); await ready(page);
  await page.click(vp.phone ? '.topbar__menu' : 'a:has-text("People")').catch(() => {});
  await page.goto(`${base}/manager/people.html`); await ready(page); await page.waitForSelector('table, .state');
  tally(await review(page, stem('people'), vp, { allowManyPrimary: true, shots: { list: '.card--table' } }));
  const text = await page.textContent('main');
  check(/Shankar Gouda/.test(text) && /Sunita Traders/.test(text) && /Pending verification/.test(text), 'pending sign-ups appear in the list');
  check(/F-\d{4}/.test(text) && new RegExp(farmerPhone).test(text) && /Land record \(RTC\) ending 4821/.test(text), 'the list shows ID, mobile number and the document with its last four characters');
  check(!/Waiting for the Pool Manager/.test(text), 'the Manager sees no pending banner');
  await page.selectOption('select[aria-label="Status"]', 'pending');
  check(!/Basavaraj/.test(await page.textContent('main')) && /Shankar/.test(await page.textContent('main')), 'the status filter shows only pending people');
  await page.selectOption('select[aria-label="Status"]', '');
  await page.goto(`${base}/manager/accounts.html`); await ready(page); await page.waitForSelector('#form-deposit');
  check(!/Shankar Gouda/.test(await page.locator('#dep-farmer').innerHTML()), 'the deposit form does not offer a farmer who is still pending');
  await page.goto(`${base}/manager/people.html`); await ready(page); await page.waitForSelector('table');
  const row = (name) => page.locator('tbody tr', { hasText: name });
  await row('Shankar Gouda').locator('button:has-text("Approve")').click();
  await page.waitForSelector('tbody tr:has-text("Shankar Gouda"):has-text("Active")');
  check(/Checked by .* on /.test(await row('Shankar Gouda').textContent()), 'approval records who checked the document and when');
  await row('Shankar Gouda').locator('button:has-text("Edit")').click(); await page.waitForSelector('.modal #p-name');
  tally(await review(page, stem('people-edit'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.fill('#p-name', 'Shankar Gowda'); await page.click('.modal button.btn--primary');
  await page.waitForSelector('.modal', { state: 'detached' }); await page.waitForSelector('tbody tr:has-text("Shankar Gowda")');
  await row('Shankar Gowda').locator('button:has-text("Deactivate")').click(); await page.waitForSelector('.modal');
  tally(await review(page, stem('people-deactivate'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.click('.modal button.btn--primary'); await page.waitForSelector('tbody tr:has-text("Shankar Gowda"):has-text("Inactive")');
  await row('Shankar Gowda').locator('button:has-text("Reactivate")').click(); await page.waitForSelector('tbody tr:has-text("Shankar Gowda"):has-text("Active")');
  // approve the buyer and add someone in person
  await row('Sunita Traders').locator('button:has-text("Approve")').click(); await page.waitForSelector('tbody tr:has-text("Sunita Traders"):has-text("Active")');
  await page.click('button:has-text("Add person")'); await page.waitForSelector('.modal #a-name');
  await page.fill('#a-name', 'Lakshmi Bai'); await page.fill('#a-phone', '12345'); await page.click('.modal button:has-text("Create account")');
  await page.waitForSelector('#a-phone-error:not([hidden])');
  tally(await review(page, stem('people-add'), vp, { allowManyPrimary: true, skipGrey: true, shots: { modal: '.modal' } }));
  await page.fill('#a-phone', `96${digits.slice(0, 8)}`); await page.click('.modal button:has-text("Create account")');
  await page.waitForSelector('.modal :text("Account created for Lakshmi Bai")');
  check(/temporary password/.test(await page.textContent('.modal')), 'the new account shows its temporary password once');
  await page.click('.modal button:has-text("Done")'); await page.waitForSelector('tbody tr:has-text("Lakshmi Bai")');
  // the Manager's forms list only verified people
  await page.goto(`${base}/manager/accounts.html`); await ready(page); await page.waitForSelector('#form-deposit');
  check(/Basavaraj/.test(await page.locator('#dep-farmer').innerHTML()) && /Shankar Gowda/.test(await page.locator('#dep-farmer').innerHTML()), 'the deposit form offers people who have been verified');
  tally(await review(page, stem('forms-without-accounts'), vp, { allowManyPrimary: true, skipGrey: true }));
  await context.close();

  // the approved buyer can now buy
  const ok = await newPage(browser, base, vp);
  await uiLogin(ok.page, buyerPhone);
  await ok.page.waitForURL(/buyer\/pools\.html/); await ready(ok.page);
  check(!/Waiting for the Pool Manager/.test(await ok.page.textContent('body')), 'the banner is gone after approval');
  await ok.page.goto(`${base}/buyer/pool.html?id=${data.pools.paddy}`); await ready(ok.page); await ok.page.waitForSelector('#quantity');
  await ok.page.waitForSelector('#market-card table');
  check(/Source: Agmarknet via data\.gov\.in/.test(await ok.page.textContent('#market-card')) && /₹[\d,]+ per quintal/.test(await ok.page.textContent('#market-card')), 'the buyer sees the day\'s market prices with their source');
  tally(await review(ok.page, stem('buyer-pool-market'), vp, { skipGrey: true, shots: { prices: '#market-card' } }));
  await ok.page.fill('#quantity', '1'); await ok.page.click('button:has-text("Review order")'); await ok.page.waitForSelector('.modal');
  await ok.page.click('.modal button.btn--primary'); await ok.page.waitForSelector('.modal :text("Purchase complete")');
  await ok.context.close();
}

const scenarios = { login, buyer, farmer, manager, people };
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
