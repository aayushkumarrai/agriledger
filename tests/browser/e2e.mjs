// End-to-end story in a real browser against the real server and database (spec Stage 8). Run through scripts/e2e_test.sh.
//  1. the Manager creates a pool, adds a photo and opens it for sale
//  2. two buyers, in two separate browser windows, race for the last stock: exactly one wins
//  3. the price changes while a buyer has the confirmation open
//  4. a double tap sends one purchase only
//  5. a stored row is tampered with, and Verify integrity names the record
// Desktop (1200 px) and an Android phone (390 px) both run the whole story.
import { execFileSync } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { startServer, launch, newPage, VIEWPORTS } from './lib.mjs';
import { seed, PASS, png } from './fixtures.mjs';

const { port, env } = await startServer();
const base = `http://127.0.0.1:${port}`;
const browser = await launch();
let failed = 0;
const check = (cond, what) => { console.log(`${cond ? 'PASS' : 'FAIL'}  ${what}`); if (!cond) failed += 1; };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const ready = (page) => page.waitForSelector('html[data-ready="true"]', { state: 'attached', timeout: 15000 });

async function login(page, username) {
  await page.goto(`${base}/login.html`); await ready(page);
  await page.fill('#username', username); await page.fill('#password', PASS);
  await page.click('button[type=submit]');
  await page.waitForURL((u) => !u.pathname.endsWith('login.html'));
  await ready(page);
}
const sql = (statement) => execFileSync('docker', ['compose', 'exec', '-T', 'db', 'psql', '-U', 'postgres', '-d', process.env.E2E_DB, '-qtA', '-c', statement], { encoding: 'utf8' });

async function story(vp) {
  const tag = `${vp.label}-${Date.now() % 100000}`;
  const data = await seed(base, env);
  const m = data.managerApi;
  console.log(`\n== ${vp.label} px ==`);

  // 1. the Manager creates a pool, adds a photo, opens it
  const mgr = await newPage(browser, base, vp);
  await login(mgr.page, data.manager.username);
  await mgr.page.goto(`${base}/manager/create.html`); await ready(mgr.page); await mgr.page.waitForSelector('#variety');
  await mgr.page.selectOption('#commodity', { label: 'Paddy' });
  await mgr.page.fill('#variety', `Sona ${tag}`);
  await mgr.page.fill('#price', '2,500'); await mgr.page.fill('#min-order', '1'); await mgr.page.fill('#packaging', '50 kg jute bags'); await mgr.page.fill('#storage', 'Navalgund godown, Dharwad');
  await mgr.page.setInputFiles('#photo-pick', { name: 'lot.png', mimeType: 'image/png', buffer: png(400, 300, [196, 160, 78], 3) });
  await mgr.page.waitForSelector('button:has-text("Open for sale"):not([disabled])');
  await mgr.page.click('button:has-text("Open for sale")');
  await mgr.page.waitForURL(/\/manager\/pool\.html\?id=/); await ready(mgr.page); await mgr.page.waitForSelector('.tag--ok');
  check(/Open/.test(await mgr.page.textContent('main')), '1. the Manager created a pool, added a photo and opened it');

  // 2. two buyers, two windows, the last 100 kg
  const make = (variety, price, grams) => data.make({ commodity: 'Maize', variety: `${variety} ${tag}`, range: 1, packaging: '50 kg HDPE bags', storage: 'Hubballi APMC yard', min: 100000, price }, [[210, 170, 90]], [[0, grams]]);
  const racePool = await make('Race', 2000, 100000);
  const a = await newPage(browser, base, vp), b = await newPage(browser, base, vp);
  for (const [w, who] of [[a, data.buyers[0]], [b, data.buyers[1]]]) {
    w.page.allowedStatuses = [409];
    await login(w.page, who.username);
    await w.page.goto(`${base}/buyer/pool.html?id=${racePool}`); await ready(w.page); await w.page.waitForSelector('#quantity');
    await w.page.fill('#quantity', '1'); await w.page.click('button:has-text("Review order")'); await w.page.waitForSelector('.modal');
  }
  await Promise.all([a, b].map((w) => w.page.click('.modal button.btn--primary')));
  const outcome = async (w) => { await w.page.waitForSelector('.modal :text-matches("Purchase complete|out of stock|available right now")', { timeout: 15000 }); return /Purchase complete/.test(await w.page.textContent('.modal')); };
  const won = [await outcome(a), await outcome(b)];
  check(won.filter(Boolean).length === 1, `2. two buyers raced for the last stock: exactly one won (${won.map((x) => (x ? 'bought' : 'refused')).join(' / ')})`);
  const loser = won[0] ? b : a;
  check(!/INSUFFICIENT|OUT_OF_STOCK|409/.test(await loser.page.textContent('.modal')), '2. the refused buyer sees a plain sentence, no code');
  check(Number((await m.get(`/pools/${racePool}`)).data.stock_g) === 0, '2. the pool is at zero stock, never negative');

  // 3. the price changes while the confirmation is open
  const pricePool = await make('Price', 2500, 1000000);
  await a.page.goto(`${base}/buyer/pool.html?id=${pricePool}`); await ready(a.page); await a.page.waitForSelector('#quantity');
  await a.page.fill('#quantity', '2'); await a.page.click('button:has-text("Review order")'); await a.page.waitForSelector('.modal');
  await m.patch(`/pools/${pricePool}`, { price_paise_per_kg: 2400 });
  await a.page.click('.modal button.btn--primary');
  await a.page.waitForSelector('.modal :text("The price has changed")');
  check((await a.page.textContent('.modal')).includes('From ₹2,500.00 to ₹2,400.00 per quintal'), '3. a price change during confirmation is shown, and nothing was bought');
  await a.page.click('.modal button:has-text("Review and confirm again")');
  await a.page.click('.modal button.btn--primary'); await a.page.waitForSelector('.modal :text("Purchase complete")');

  // 4. a double tap sends one purchase
  const tapPool = await make('Tap', 2500, 1000000);
  await b.page.goto(`${base}/buyer/pool.html?id=${tapPool}`); await ready(b.page); await b.page.waitForSelector('#quantity');
  await b.page.fill('#quantity', '1'); await b.page.click('button:has-text("Review order")'); await b.page.waitForSelector('.modal');
  let posts = 0;
  await b.page.route('**/sales', (route) => { posts += 1; return route.continue(); });
  await b.page.dblclick('.modal button.btn--primary');
  await b.page.waitForSelector('.modal :text("Purchase complete")');
  await sleep(300);
  check(posts === 1, `4. a double tap sent ${posts} purchase request(s)`);

  // 5. tamper, then Verify integrity
  await mgr.page.goto(`${base}/manager/pool.html?id=${racePool}`); await ready(mgr.page); await mgr.page.waitForSelector('button:has-text("Verify integrity")');
  await mgr.page.click('button:has-text("Verify integrity")');
  await mgr.page.waitForSelector('text=/All \\d+ records match/');
  check(true, '5. before tampering: all records match');
  sql(`SET session_replication_role = replica; UPDATE financial_ledger SET amount_paise = amount_paise + 1 WHERE ctid = (SELECT ctid FROM financial_ledger WHERE pool_id = ${racePool} AND entry_type = 'SELLER_PAYOUT' LIMIT 1);`);
  await mgr.page.reload(); await ready(mgr.page); await mgr.page.waitForSelector('button:has-text("Verify integrity")');
  await mgr.page.click('button:has-text("Verify integrity")');
  await mgr.page.waitForSelector('text=/does not match/', { timeout: 15000 });
  const said = await mgr.page.textContent('main');
  check(/Record \d+ \(.+\) does not match/.test(said) && !/INV-|HASH|CHAIN|500/.test(said), '5. after tampering: Verify integrity names the record in plain words');
  for (const x of [mgr, a, b]) await x.context.close();
}

try {
  for (const vp of VIEWPORTS) await story(vp);
} catch (e) { console.log('FAIL  ', e.message); failed += 1; }
await browser.close();
console.log(failed ? `\n${failed} end-to-end check(s) FAILED` : '\nAll end-to-end checks passed.');
process.exit(failed ? 1 : 0);
