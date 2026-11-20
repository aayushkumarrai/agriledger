#!/bin/bash
# Loads the demo data into the dev database through the real server. Run once on a fresh database
# (reset first with: docker compose down -v). Logins and passwords are in README.md.
set -euo pipefail
cd "$(dirname "$0")/.."
set -a; . <(tr -d '\r' < .env); set +a
docker compose up -d --wait db >/dev/null 2>&1
cmake --build build -j"$(nproc)" --target agriledger_server >/dev/null
export AGRILEDGER_DB="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=agriledger user=$APP_DB_USER password=$APP_DB_PASSWORD"
export AGRILEDGER_BIN="$PWD/build/engines/e4_web/agriledger_server"
export AGRILEDGER_PORT=0 AGRILEDGER_WEB_DIR="$PWD/web"
# pool numbers start at 12 so the demo pool is P-12, as in the demo script (only when no pool exists yet)
docker compose exec -T db psql -U postgres -d agriledger -qtA -c "DO \$\$ BEGIN IF NOT EXISTS (SELECT 1 FROM pool) THEN PERFORM setval(pg_get_serial_sequence('pool','pool_id'), 11); END IF; END \$\$;"
log=$(mktemp); "$AGRILEDGER_BIN" >"$log" 2>&1 & pid=$!
trap 'kill $pid 2>/dev/null || true; rm -f "$log"' EXIT
for _ in $(seq 1 50); do port=$(sed -n 's/.*listening on [0-9.]*:\([0-9]*\).*/\1/p' "$log"); [ -n "$port" ] && break; sleep 0.2; done
node scripts/seed_demo.mjs "http://127.0.0.1:$port"
