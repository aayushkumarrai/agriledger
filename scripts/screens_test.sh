#!/bin/bash
# The real screens against the real server and a fresh test database (needs Docker): seeds realistic data through the API,
# logs in through the login screen, walks each role's screens at 1200 px and as an Android phone, saves screenshots to
# tests/browser/out/ and runs the audits. Usage: scripts/screens_test.sh [login|buyer|farmer|manager|all]
set -euo pipefail
cd "$(dirname "$0")/.."
set -a; . <(tr -d '\r' < .env); set +a
scripts/setup_browser_libs.sh >/dev/null
docker compose up -d --wait db >/dev/null 2>&1
PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_ui_test PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" bash db/tests/setup_test_db.sh >/dev/null
export AGRILEDGER_DB="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=agriledger_ui_test user=$APP_DB_USER password=$APP_DB_PASSWORD"
cmake --build build -j"$(nproc)" --target agriledger_server >/dev/null
exec node tests/browser/screens.mjs "${1:-all}"
