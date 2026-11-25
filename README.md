# AgriLedger

Shared-inventory and proportional-payout system (Team 05 mini project). C++17, PostgreSQL 16, Crow, vanilla JavaScript.
Read `CLAUDE.md` (rules) and `docs/IMPLEMENTATION_SPEC.md` (the spec) first.

## Prerequisites (Ubuntu / WSL)
```bash
sudo apt-get install -y build-essential cmake postgresql-client nodejs npm \
  libpqxx-dev libssl-dev libsodium-dev nlohmann-json3-dev libgtest-dev libasio-dev
```
Docker (with Compose) runs PostgreSQL. In WSL, use Docker Desktop with WSL integration turned on.
Crow is downloaded by CMake at a pinned release; Playwright is installed later, with the frontend.

## First-time setup
```bash
cp .env.example .env        # then put your own development passwords in .env (git-ignored)
docker compose up -d --wait db
```
The first start loads `db/schema.sql` and `db/seed_reference.sql` and creates the application login user.
To start over: `docker compose down -v`.

## Build
```bash
cmake -S . -B build && cmake --build build -j
```

## Test (one command)
```bash
scripts/test.sh          # database tests, then the C++ tests
scripts/test.sh db       # only db/tests: worked_example.sql (INV-1..5 all OK) and negative.sh (34 PASS)
scripts/test.sh cpp      # only the C++ unit tests
```
The database tests use a throw-away database `agriledger_test` in the same container and read their connection from
`PGHOST`, `PGPORT`, `PGUSER`, `PGDATABASE` (and `PGPASSWORD`); `scripts/test.sh` fills them in from `.env`.

## Run the server
```bash
cmake --build build -j
echo 'a-temporary-password' | scripts/run_server.sh create-manager alice "Alice Pool Manager"   # once; no self-registration
scripts/run_server.sh                                                                            # http://127.0.0.1:8080
```
The Manager must choose a new password at first login, then creates Farmer and Buyer accounts through the API.
Settings (environment): `AGRILEDGER_DB` (set by the script from `.env`), `AGRILEDGER_PORT`, `AGRILEDGER_BIND`,
`AGRILEDGER_PHOTO_DIR`, `AGRILEDGER_COOKIE_SECURE=1` when served over HTTPS. Crow and Asio are fetched by CMake at pinned releases
(a system `libasio-dev` is used if present).

## Demo data and logins
```bash
scripts/seed_demo.sh     # once, on a fresh database (docker compose down -v first if you already created accounts)
scripts/run_server.sh    # then open http://localhost:8080
```
The seed goes through the real server, so every ledger row, hash and receipt is genuine. Pool numbers start at 12.
Every demo password is `Demo-pass-2026`.

| Role | Sign in with | Name | ID |
|---|---|---|---|
| Pool Manager | `manager` | Kavita Desai | M-0001 |
| Farmer A | `farmer.a` or `9845000001` | Basavaraj Hiremath | F-0002 |
| Farmer B | `farmer.b` or `9845000002` | Renukamma Patil | F-0003 |
| Farmer C | `farmer.c` or `9845000003` | Mallikarjun Kulkarni | F-0004 |
| Buyer 1 | `buyer.1` or `9845000004` | Mahantesh Traders | B-0005 |
| Buyer 2 | `buyer.2` or `9845000005` | Gangadhar Agro Mart | B-0006 |
| Farmer, pending | `9845000006` | Shivappa Naik | F-0007 |

New people sign up themselves on the login page (**New here? Sign up**), choosing Farmer or Buyer, a mobile number, and an identity document. The account is
pending until the Pool Manager approves it on the **People** page. There is no Pool Manager sign-up: that account is created on the server with
`create-manager`.

Demo lot photos are real photographs from Wikimedia Commons: credits and licences are in `scripts/demo-photos/CREDITS.md` (replace those three files to use your own).
Pools: P-12 paddy (Sona Mahsuri, Category 1, 600 + 800 + 600 kg, open), P-13 groundnut (Gejje, open), P-14 maize (sold out). Each pool's price starts from the market's median price for its variety on the day the demo data is loaded.
Evaluators: follow `DEMO_SCRIPT.md`. On an Android phone on the same Wi-Fi, start the server with `AGRILEDGER_BIND=0.0.0.0` and open `http://<your PC's address>:8080`.

## Market prices (Agmarknet via data.gov.in)
The Pool Manager's **Pools** page has an **Update market prices** button. It fetches today's mandi prices for Karnataka (variety, market grade, lowest, highest and most common price,
by market) from the free data.gov.in service, which publishes Agmarknet's daily file, and saves them. The Create pool page shows them while the Manager chooses the variety and sets a price;
buyers and farmers see them on their pool pages. Prices are always shown with their date and the source line "Agmarknet via data.gov.in".
1. Create a free account on data.gov.in and copy your API key (My Account).
2. Put it in `.env` as `DATA_GOV_API_KEY=...` (this file is never committed).
3. Restart `scripts/run_server.sh` and press **Update market prices**.
Without a key the button says prices are not set up. For an offline demo set `AGRILEDGER_MARKET_FEED_FILE=tests/fixtures/market_sample.json` (saved real quotes from 25 Nov 2026);
`scripts/seed_demo.sh` does this by itself when there is no key. A failed update never removes the last saved prices.

## Browser tests
```bash
scripts/screens_test.sh all   # every screen at 1200 px and 390 px, four states each, plus the audits
scripts/e2e_test.sh           # the end-to-end story: create pool, two-buyer race, price change, double tap, tamper
```

Concurrency demonstration (protected path against the harness-only unprotected counter): `scripts/demo_concurrency.sh`.

## Frontend
Plain HTML, CSS and JavaScript in `web/`, served by the C++ server (no build step, no CDN: the fonts are self-hosted).
Two pages exist so far: `/styleguide.html` (the style board from `docs/wireframes/Main.png`) and `/styleguide-states.html`
(every control in every state). All visible text comes from `web/i18n/en.json`.
```bash
npm install                       # once: Playwright is a test-only dependency
npx playwright install chromium   # once: the browser used by the checks
scripts/test.sh js                # unit tests for format.js (exact BigInt maths), i18n.js, api.js, live.js
scripts/browser_test.sh           # screenshots at 1200 and 390 px plus the audits; output in tests/browser/out/
```
The browser checks need two small system libraries (`libnss3`, `libnspr4`); if they are not installed,
`scripts/setup_browser_libs.sh` unpacks them locally without root. Playwright is pinned to 1.49.1 because it is the newest
release that still supports Node 18. The audits check spacing against the 4/8/12/16/20/24/32 scale, colours against the tokens,
contrast, tap targets, number alignment, fonts, photo aspect ratios, icon stroke width, shadows and gradients; they also save
greyscale and blurred ("squint") screenshots.

## Supported browsers
Desktop Chrome, Edge and Firefox, and Chrome on Android, are the targets; the browser checks run Chromium (the Android Chrome
engine) at 1200 px and as a 390 px touch phone. iPhone Safari is expected to work but is not a priority: nothing is tuned for it
yet, and any iPhone-only fix (for example 16 px inputs to stop focus-zoom, or naming the host for WebSockets in the
Content-Security-Policy on Safari before 15.4) is added only when it is needed. Phone layouts start below 720 px.

## Layout
See spec §2. Engines: `engines/e1_audit` (allocation and hashing), `e2_sale` (SQL transactions), `e3_queue` (per-pool queue),
`e4_web` (server). Frontend in `web/`. Tests in `tests/` and `db/tests/`.

## Credits
Team 05 · Theme 04 (Transaction Based Inventory Management System). Engines: E1 Ayush Kumar Rai, E2 Shubhang R, E3 Sakshi K,
E4 Jairaj Kavatagimath.
