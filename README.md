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
