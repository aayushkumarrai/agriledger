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

## Layout
See spec §2. Engines: `engines/e1_audit` (allocation and hashing), `e2_sale` (SQL transactions), `e3_queue` (per-pool queue),
`e4_web` (server). Frontend in `web/`. Tests in `tests/` and `db/tests/`.

## Credits
Team 05 · Theme 04 (Transaction Based Inventory Management System). Engines: E1 Ayush Kumar Rai, E2 Shubhang R, E3 Sakshi K,
E4 Jairaj Kavatagimath.
