#!/bin/bash
# One command for every test that exists so far. Starts the database container if needed.
# Usage: scripts/test.sh [db|cpp|js|browser|all]   (default: all; browser runs only if npm packages are installed)
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] || { echo "Missing .env: copy .env.example to .env first"; exit 1; }
set -a; . <(tr -d '\r' < .env); set +a      # tolerate Windows line endings
what="${1:-all}"
case "$what" in db|cpp|js|browser|all) ;; *) echo "Usage: scripts/test.sh [db|cpp|js|browser|all]" >&2; exit 2 ;; esac

if [ "$what" = db ] || [ "$what" = all ]; then
  docker compose up -d --wait db >/dev/null
  PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_test \
  PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" \
  PGUSER="$APP_DB_USER" PGPASSWORD="$APP_DB_PASSWORD" bash db/tests/run_all.sh
fi
if [ "$what" = cpp ] || [ "$what" = all ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build -j"$(nproc)"
  # a fresh database for the C++ integration tests
  docker compose up -d --wait db >/dev/null
  PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_cpp_test \
  PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" bash db/tests/setup_test_db.sh
  base="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=agriledger_cpp_test"
  AGRI_TEST_CONNINFO="$base user=$APP_DB_USER password=$APP_DB_PASSWORD" \
  AGRI_TEST_SUPER_CONNINFO="$base user=postgres password=$POSTGRES_PASSWORD" \
  ctest --test-dir build --output-on-failure
fi
if [ "$what" = js ] || [ "$what" = all ]; then
  [ -d node_modules ] || npm install --no-audit --no-fund >/dev/null
  node --test tests/js/
fi
if [ "$what" = browser ] || { [ "$what" = all ] && [ -d node_modules/playwright ] && [ -d "$HOME/.cache/ms-playwright" ]; }; then
  scripts/browser_test.sh
fi
