#!/bin/bash
# Shows protected (E3 queue + E2 row lock) against the harness-only unprotected counter. Needs a build in ./build.
set -euo pipefail
cd "$(dirname "$0")/.."
set -a; . <(tr -d '\r' < .env); set +a
docker compose up -d --wait db >/dev/null 2>&1
PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_cpp_test PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" bash db/tests/setup_test_db.sh >/dev/null
base="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=agriledger_cpp_test"
AGRI_TEST_CONNINFO="$base user=$APP_DB_USER password=$APP_DB_PASSWORD" \
AGRI_TEST_SUPER_CONNINFO="$base user=postgres password=$POSTGRES_PASSWORD" \
  build/tests/concurrency_demo
