#!/bin/bash
# Runs the server (or a server command such as create-manager) against the dev database from .env.
#   scripts/run_server.sh                                  start on 127.0.0.1:8080
#   echo 'TempPass123' | scripts/run_server.sh create-manager alice "Alice Pool Manager"
# Set AGRILEDGER_DBNAME to use another database (the tests use agriledger_cpp_test).
set -euo pipefail
cd "$(dirname "$0")/.."
set -a; . <(tr -d '\r' < .env); set +a
export AGRILEDGER_DB="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=${AGRILEDGER_DBNAME:-agriledger} user=$APP_DB_USER password=$APP_DB_PASSWORD"
docker compose up -d --wait db >/dev/null 2>&1
exec build/engines/e4_web/agriledger_server "$@"
