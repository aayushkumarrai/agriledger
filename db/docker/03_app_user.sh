#!/bin/bash
# Runs once when the PostgreSQL container first initialises (after schema.sql and seed_reference.sql).
# Creates the LOGIN user the application connects as. It is a member of the NOLOGIN role
# agriledger_app, which schema.sql grants the least-privilege rights to.
set -e
psql -v ON_ERROR_STOP=1 -v app_user="$APP_DB_USER" -v app_pw="$APP_DB_PASSWORD" \
     --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<'SQL'
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', :'app_user', :'app_pw') \gexec
GRANT agriledger_app TO :"app_user";
SQL
