#!/bin/bash
# End-to-end story in Chromium (desktop and Android phone) against the real server and a fresh test database. Needs Docker.
set -euo pipefail
cd "$(dirname "$0")/.."
set -a; . <(tr -d '\r' < .env); set +a
scripts/setup_browser_libs.sh >/dev/null
docker compose up -d --wait db >/dev/null 2>&1
export E2E_DB=agriledger_e2e_test
PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=$E2E_DB PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" bash db/tests/setup_test_db.sh >/dev/null
export AGRILEDGER_DB="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=$E2E_DB user=$APP_DB_USER password=$APP_DB_PASSWORD"
cmake --build build -j"$(nproc)" --target agriledger_server >/dev/null
exec node tests/browser/e2e.mjs
