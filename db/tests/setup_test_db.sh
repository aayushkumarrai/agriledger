#!/bin/bash
# Linux only, run from the repo root. On other systems use docker-compose (spec §12, Stage 1).
# Fresh throw-away PostgreSQL cluster for schema tests (socket dir /tmp/pgd, port 5544).
set -e
PGBIN=/usr/lib/postgresql/16/bin
pkill -u postgres -f "postgres -D /tmp/pgd/data" 2>/dev/null || true; sleep 1
rm -rf /tmp/pgd; mkdir -p /tmp/pgd; chown postgres:postgres /tmp/pgd
su postgres -c "$PGBIN/initdb -D /tmp/pgd/data -A trust -U postgres >/dev/null"
su postgres -c "$PGBIN/pg_ctl -D /tmp/pgd/data -o '-p 5544 -k /tmp/pgd -c listen_addresses=' -l /tmp/pgd/log -w start >/dev/null"
P="psql -h /tmp/pgd -p 5544 -U postgres -q -v ON_ERROR_STOP=1"
$P -c "CREATE DATABASE agri"
$P -d agri -f db/schema.sql
$P -d agri -f db/seed_reference.sql
$P -d agri -c "CREATE ROLE app_test LOGIN; GRANT agriledger_app TO app_test"
echo "test database ready"
