// Shared by the browser checks: start the real server, launch Chromium, capture screenshots and run the audits on a page.
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { pageAudit } from './audits.mjs';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
export const OUT = join(ROOT, 'tests', 'browser', 'out');
mkdirSync(OUT, { recursive: true });

// Desktop first, then an Android phone: Chromium is the same engine as Chrome on Android, with touch and a phone user agent.
const ANDROID_UA = 'Mozilla/5.0 (Linux; Android 14; Pixel 7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Mobile Safari/537.36';
export const VIEWPORTS = [
  { label: '1200', width: 1200, height: 900 },
  { label: '390', width: 390, height: 844, phone: { isMobile: true, hasTouch: true, userAgent: ANDROID_UA } },
];

export const SERVER_BIN = join(ROOT, 'build', 'engines', 'e4_web', 'agriledger_server');

export function startServer(dbConninfo) {
  return new Promise((resolve, reject) => {
    if (!existsSync(SERVER_BIN)) return reject(new Error('build the server first: cmake --build build --target agriledger_server'));
    const db = dbConninfo || process.env.AGRILEDGER_DB || 'host=127.0.0.1 port=1 dbname=none user=none connect_timeout=1';
    const photoDir = join(OUT, 'photos-' + process.pid);
    const env = { ...process.env, AGRILEDGER_DB: db, AGRILEDGER_PORT: '0', AGRILEDGER_WEB_DIR: join(ROOT, 'web'), AGRILEDGER_PHOTO_DIR: photoDir, AGRILEDGER_MARKET_FEED_FILE: join(ROOT, 'tests', 'fixtures', 'market_sample.json') };
    const child = spawn(SERVER_BIN, [], { env, stdio: ['ignore', 'pipe', 'ignore'] });
    let buffer = '';
    child.stdout.on('data', (d) => {
      buffer += d.toString();
      const m = /listening on [\d.]+:(\d+)/.exec(buffer);
      if (m) resolve({ child, port: Number(m[1]), env });
    });
    child.on('exit', (code) => reject(new Error(`server exited early (${code})`)));
    setTimeout(() => reject(new Error('server did not start')), 20000);
  });
}

export async function launch() {
  const libs = join(ROOT, 'tests', 'browser', '.libs', 'root', 'usr', 'lib', 'x86_64-linux-gnu');
  const env = existsSync(libs) ? { ...process.env, LD_LIBRARY_PATH: `${libs}:${process.env.LD_LIBRARY_PATH || ''}` } : process.env;
  return chromium.launch({ env });
}

export async function newPage(browser, base, vp) {
  const context = await browser.newContext({ viewport: { width: vp.width, height: vp.height }, deviceScaleFactor: 1, locale: 'en-IN', timezoneId: 'Asia/Kolkata', ...(vp.phone || {}) });
  const page = await context.newPage();
  const problems = [];
  page.problems = problems;
  page.on('console', (m) => {
    if (m.type() !== 'error' && m.type() !== 'warning') return;
    const expected = (page.allowedStatuses || []).some((code) => m.text().includes(`status of ${code}`));   // the browser logs every 4xx
    if (!expected) problems.push({ check: 'console', detail: m.text() });
  });
  page.on('pageerror', (e) => problems.push({ check: 'script-error', detail: String(e) }));
  page.on('request', (r) => { if (!r.url().startsWith(base) && !r.url().startsWith('data:') && !r.url().startsWith('blob:') && !r.url().startsWith('ws://127.0.0.1')) problems.push({ check: 'offline', detail: `request leaves the site: ${r.url()}` }); });
  page.on('response', (r) => { if (r.status() >= 400 && !page.allowedStatuses?.includes(r.status())) problems.push({ check: 'http-error', detail: `${r.status()} ${r.url()}` }); });
  return { context, page };
}

// Screenshots (normal, greyscale, squint, optional close-ups), then the audits. Returns the list of failures.
export async function review(page, stem, vp, { shots = {}, allowManyPrimary = false, skipGrey = false } = {}) {
  await page.evaluate(() => document.fonts.ready);
  await page.evaluate(() => Promise.all(document.getAnimations().map((a) => a.finished.catch(() => {}))));   // let transitions settle before sampling colours
  await page.waitForTimeout(100);
  const audit = await page.evaluate(pageAudit, { viewportWidth: vp.width, allowManyPrimary });
  await page.screenshot({ path: join(OUT, `${stem}.png`), fullPage: true });
  for (const [name, selector] of Object.entries(shots)) {
    const loc = page.locator(selector).first();
    if (await loc.count()) await loc.screenshot({ path: join(OUT, `${stem}-${name}.png`) });
  }
  if (!skipGrey) {
    await page.evaluate(() => { document.documentElement.style.filter = 'grayscale(1)'; });
    await page.screenshot({ path: join(OUT, `${stem}-grey.png`), fullPage: true });
    await page.evaluate(() => { document.documentElement.style.filter = 'grayscale(1) blur(5px)'; });
    await page.screenshot({ path: join(OUT, `${stem}-squint.png`), fullPage: true });
    await page.evaluate(() => { document.documentElement.style.filter = ''; });
  }
  const all = [...page.problems.splice(0), ...audit.failures];
  console.log(`${all.length ? 'FAIL' : 'PASS'}  ${stem}  ${audit.notes.join('; ')}`);
  for (const f of all.slice(0, 30)) console.log(`      [${f.check}] ${f.detail}`);
  if (all.length > 30) console.log(`      ... and ${all.length - 30} more`);
  return all;
}
