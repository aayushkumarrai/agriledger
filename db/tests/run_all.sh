#!/bin/bash
# Database tests in one command: fresh test DB -> worked_example.sql (INV-1..5 all OK) -> negative.sh (all PASS).
# Run from the repo root. Environment as in setup_test_db.sh, plus PGUSER/PGPASSWORD for the application role.
set -uo pipefail
: "${PGUSER:?set PGUSER (application role login)}" "${PGPASSWORD:?set PGPASSWORD}"
APP_USER="$PGUSER"; APP_PASSWORD="$PGPASSWORD"
fail=0
bash db/tests/setup_test_db.sh || exit 1
export PGUSER="$APP_USER" PGPASSWORD="$APP_PASSWORD"

out=$(psql -At -q -v ON_ERROR_STOP=1 -f db/tests/worked_example.sql 2>&1); rc=$?
ok=$(printf '%s\n' "$out" | grep -cE '^INV-[1-5]\|OK$')
if [ $rc -ne 0 ] || [ "$ok" -ne 5 ] || printf '%s\n' "$out" | grep -qE '^INV-[1-5]\|FAIL'; then
  echo "FAIL  worked_example.sql ($ok of 5 invariants OK)"; printf '%s\n' "$out" | tail -15; fail=1
else echo "PASS  worked_example.sql: INV-1..INV-5 all OK"; fi

nout=$(bash db/tests/negative.sh 2>&1)
printf '%s\n' "$nout" | grep -E '^(FAIL|---)' || true
pass=$(printf '%s\n' "$nout" | grep -c '^PASS'); bad=$(printf '%s\n' "$nout" | grep -c '^FAIL')
expected=$(grep -cE '^(t|ok) ' db/tests/negative.sh)   # every check in the file must run and PASS
if [ "$bad" -ne 0 ] || [ "$pass" -ne "$expected" ]; then echo "FAIL  negative.sh: $pass PASS, $bad FAIL (expected $expected PASS, 0 FAIL)"; fail=1
else echo "PASS  negative.sh: $pass of $expected checks PASS"; fi
exit $fail
