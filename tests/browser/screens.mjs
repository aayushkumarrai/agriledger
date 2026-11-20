// Real screens, real server, real database. Usage: node tests/browser/screens.mjs [login|buyer|farmer|manager|all]
// Run through scripts/screens_test.sh (it prepares the database). Screenshots go to tests/browser/out/.
import { startServer, launch, newPage, review, VIEWPORTS } from './lib.mjs';
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

const scenarios = { login };
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
