#!/bin/bash
# Creates a fresh, empty test database (PGDATABASE) in the running PostgreSQL container and loads
# db/schema.sql and db/seed_reference.sql into it as the superuser. Run from the repo root.
# Environment: PGHOST PGPORT PGDATABASE PGSUPERUSER(default postgres) PGSUPERPASSWORD
set -euo pipefail
: "${PGHOST:?}" "${PGPORT:?}" "${PGDATABASE:?}" "${PGSUPERPASSWORD:?}"
: "${PGSUPERUSER:=postgres}"
case "$PGDATABASE" in
  *_test) ;;
  *) echo "Refusing to drop and recreate '$PGDATABASE': the test database name must end in _test" >&2; exit 1 ;;
esac
export PGUSER="$PGSUPERUSER" PGPASSWORD="$PGSUPERPASSWORD"
psql -q -v ON_ERROR_STOP=1 -d postgres -c "DROP DATABASE IF EXISTS \"$PGDATABASE\" WITH (FORCE)" -c "CREATE DATABASE \"$PGDATABASE\""
psql -q -v ON_ERROR_STOP=1 -d "$PGDATABASE" -f db/schema.sql
psql -q -v ON_ERROR_STOP=1 -d "$PGDATABASE" -f db/seed_reference.sql
echo "test database $PGDATABASE ready"
