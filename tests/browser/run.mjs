// Browser checks: starts the real server, opens each page at 1200 px and 390 px, saves normal / greyscale / squint screenshots
// to tests/browser/out/ and runs the audits in audits.mjs. Exit code 1 if any audit fails.
// The pages are static files, so no database is needed: unless AGRILEDGER_DB is set the server is pointed at an unreachable one
// (its live-feed listener just keeps retrying quietly). Optional: PAGES=styleguide,states
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { chromium } from 'playwright';
import { pageAudit } from './audits.mjs';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
const OUT = join(ROOT, 'tests', 'browser', 'out');
mkdirSync(OUT, { recursive: true });

const ALL_PAGES = [
  { name: 'styleguide', path: '/styleguide.html', allowManyPrimary: false },
  { name: 'states', path: '/styleguide-states.html', allowManyPrimary: true,
    shots: { buttons: 'section:has(.btn--primary)', photos: 'section:has(.photo--add)', nav: 'section:has(.topbar)', popup: '.modal' } },
];
const wanted = (process.env.PAGES || '').split(',').filter(Boolean);
const PAGES = ALL_PAGES.filter((p) => existsSync(join(ROOT, 'web', p.path)) && (!wanted.length || wanted.includes(p.name)));
const VIEWPORTS = [{ label: '1200', width: 1200, height: 900 }, { label: '390', width: 390, height: 844 }];

function startServer() {
  return new Promise((resolve, reject) => {
    const bin = join(ROOT, 'build', 'engines', 'e4_web', 'agriledger_server');
    if (!existsSync(bin)) return reject(new Error('build the server first: cmake --build build --target agriledger_server'));
    const db = process.env.AGRILEDGER_DB || 'host=127.0.0.1 port=1 dbname=none user=none connect_timeout=1';
    const child = spawn(bin, [], { env: { ...process.env, AGRILEDGER_DB: db, AGRILEDGER_PORT: '0', AGRILEDGER_WEB_DIR: join(ROOT, 'web') }, stdio: ['ignore', 'pipe', 'ignore'] });
    let buffer = '';
    child.stdout.on('data', (d) => {
      buffer += d.toString();
      const m = /listening on [\d.]+:(\d+)/.exec(buffer);
      if (m) resolve({ child, port: Number(m[1]) });
    });
    child.on('exit', (code) => reject(new Error(`server exited early (${code})`)));
    setTimeout(() => reject(new Error('server did not start')), 20000);
  });
}

const libs = join(ROOT, 'tests', 'browser', '.libs', 'root', 'usr', 'lib', 'x86_64-linux-gnu');
const launchEnv = existsSync(libs) ? { ...process.env, LD_LIBRARY_PATH: `${libs}:${process.env.LD_LIBRARY_PATH || ''}` } : process.env;

const { child, port } = await startServer();
const base = `http://127.0.0.1:${port}`;
const browser = await chromium.launch({ env: launchEnv });
let failed = 0;
const report = [];

try {
  for (const pg of PAGES) {
    for (const vp of VIEWPORTS) {
      const context = await browser.newContext({ viewport: { width: vp.width, height: vp.height }, deviceScaleFactor: 1, locale: 'en-IN', timezoneId: 'Asia/Kolkata' });
      const page = await context.newPage();
      const problems = [];
      page.on('console', (m) => { if (m.type() === 'error' || m.type() === 'warning') problems.push({ check: 'console', detail: m.text() }); });
      page.on('pageerror', (e) => problems.push({ check: 'script-error', detail: String(e) }));
      page.on('request', (r) => { if (!r.url().startsWith(base) && !r.url().startsWith('data:')) problems.push({ check: 'offline', detail: `request leaves the site: ${r.url()}` }); });
      page.on('response', (r) => { if (r.status() >= 400) problems.push({ check: 'missing-file', detail: `${r.status()} ${r.url()}` }); });

      await page.goto(base + pg.path, { waitUntil: 'networkidle' });
      try {
        await page.waitForSelector('html[data-ready="true"]', { state: 'attached', timeout: 10000 });   // no eval: the page's CSP stays on
      } catch {
        problems.push({ check: 'not-ready', detail: 'the page never finished drawing (see the errors above it)' });
      }
      await page.evaluate(() => document.fonts.ready);

      const stem = `${pg.name}-${vp.label}`;
      await page.screenshot({ path: join(OUT, `${stem}.png`), fullPage: true });
      const audit = await page.evaluate(pageAudit, { viewportWidth: vp.width, allowManyPrimary: pg.allowManyPrimary });
      for (const [shot, selector] of Object.entries(pg.shots || {})) await page.locator(selector).first().screenshot({ path: join(OUT, `${stem}-${shot}.png`) });   // close-ups for review
      // greyscale test: meaning and hierarchy must survive without colour
      await page.evaluate(() => { document.documentElement.style.filter = 'grayscale(1)'; });   // CSSOM, not an inline <style>
      await page.screenshot({ path: join(OUT, `${stem}-grey.png`), fullPage: true });
      // squint test: blur it, and see what still stands out
      await page.evaluate(() => { document.documentElement.style.filter = 'grayscale(1) blur(5px)'; });
      await page.screenshot({ path: join(OUT, `${stem}-squint.png`), fullPage: true });

      const all = [...problems, ...audit.failures];
      failed += all.length;
      report.push({ page: pg.name, viewport: vp.label, failures: all, notes: audit.notes });
      console.log(`${all.length ? 'FAIL' : 'PASS'}  ${stem}  ${audit.notes.join('; ')}`);
      for (const f of all.slice(0, 40)) console.log(`      [${f.check}] ${f.detail}`);
      if (all.length > 40) console.log(`      ... and ${all.length - 40} more`);
      await context.close();
    }
  }
} finally {
  writeFileSync(join(OUT, 'report.json'), JSON.stringify(report, null, 2));
  await browser.close();
  child.kill();
}
console.log(failed ? `\n${failed} problem(s). Screenshots are in tests/browser/out/` : '\nAll browser checks passed. Screenshots are in tests/browser/out/');
process.exit(failed ? 1 : 0);
