#!/bin/bash
# One command for every test that exists so far. Starts the database container if needed.
# Usage: scripts/test.sh [db|cpp|all]   (default: all)
set -euo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] || { echo "Missing .env: copy .env.example to .env first"; exit 1; }
set -a; . <(tr -d '\r' < .env); set +a      # tolerate Windows line endings
what="${1:-all}"
case "$what" in db|cpp|all) ;; *) echo "Usage: scripts/test.sh [db|cpp|all]" >&2; exit 2 ;; esac

if [ "$what" = db ] || [ "$what" = all ]; then
  docker compose up -d --wait db >/dev/null
  PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_test \
  PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" \
  PGUSER="$APP_DB_USER" PGPASSWORD="$APP_DB_PASSWORD" bash db/tests/run_all.sh
fi
if [ "$what" = cpp ] || [ "$what" = all ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build -j"$(nproc)"
  ctest --test-dir build --output-on-failure
fi
