#!/bin/bash
# Prints the command output that the Sprint 2 handbook asks you to screenshot (Section 10), one part at a time.
# Every part starts with a header showing what is being run, the date and the commit, so each screenshot explains itself.
#   scripts/evidence.sh db          database tests: worked example (INV-1 to INV-5) and negative.sh (47 checks)
#   scripts/evidence.sh db-detail   the same, with every table and every PASS line shown (use this for the Section 10 log)
#   scripts/evidence.sh e1          E1 tests (27)
#   scripts/evidence.sh e1-worked   E1 worked example and rounding example, with their test names
#   scripts/evidence.sh e2          E2 C++ tests (26)
#   scripts/evidence.sh e3          E3 queue and concurrency tests (17)
#   scripts/evidence.sh oversell    the 50 buyers by 20 runs result: protected 0 of 20, unprotected 20 of 20
#   scripts/evidence.sh e4          E4 tests (36)
#   scripts/evidence.sh all-cpp     the whole C++ run: 107 tests
#   scripts/evidence.sh js          JavaScript tests (40)
#   scripts/evidence.sh static      the static checks (no float or double, no literal UI strings)
#   scripts/evidence.sh screens     every screen at 1200 px and 390 px (takes a few minutes)
#   scripts/evidence.sh e2e         the end-to-end story (takes a few minutes)
#   scripts/evidence.sh api         real requests and responses from the running server (start scripts/run_server.sh first)
set -uo pipefail
cd "$(dirname "$0")/.."
[ -f .env ] || { echo "Missing .env: copy .env.example to .env first"; exit 1; }
set -a; . <(tr -d '\r' < .env); set +a

header() {
  echo
  echo "=== $1"
  echo "=== $(date '+%d %b %Y, %H:%M') | commit $(git rev-parse --short HEAD) | branch $(git branch --show-current)"
  echo
}

cpp_db() {   # a fresh database for the C++ integration tests, exactly as scripts/test.sh cpp prepares it
  docker compose up -d --wait db >/dev/null 2>&1
  PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGDATABASE=agriledger_cpp_test \
  PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" bash db/tests/setup_test_db.sh >/dev/null 2>&1
  base="host=127.0.0.1 port=${POSTGRES_PORT:-5432} dbname=agriledger_cpp_test"
  export AGRI_TEST_CONNINFO="$base user=$APP_DB_USER password=$APP_DB_PASSWORD"
  export AGRI_TEST_SUPER_CONNINFO="$base user=postgres password=$POSTGRES_PASSWORD"
}

ctest_run() {   # ctest_run "<heading>" <ctest regex>
  header "$1"
  echo "\$ ctest --test-dir build -R \"$2\""
  ctest --test-dir build -R "$2" 2>&1 | grep -E "Test +#|tests passed|tests failed|Total Test time" | sed -E 's/ +Test +#[0-9]+: / /; s/ \.{3,}/  /'
}

case "${1:-help}" in
  db)
    header "Database tests: worked_example.sql and negative.sh"
    echo "\$ scripts/test.sh db"
    scripts/test.sh db 2>&1 | grep -v "^ Container" ;;
  db-detail)
    header "Database tests in detail: worked_example.sql and negative.sh"
    export PGHOST=127.0.0.1 PGPORT="${POSTGRES_PORT:-5432}" PGSUPERUSER=postgres PGSUPERPASSWORD="$POSTGRES_PASSWORD" PGDATABASE=agriledger_test
    docker compose up -d --wait db >/dev/null 2>&1
    bash db/tests/setup_test_db.sh >/dev/null 2>&1
    export PGUSER="$APP_DB_USER" PGPASSWORD="$APP_DB_PASSWORD"
    echo "\$ psql -f db/tests/worked_example.sql"
    psql -q -f db/tests/worked_example.sql 2>&1 | head -60
    echo
    echo "\$ ./db/tests/negative.sh"
    bash db/tests/negative.sh 2>&1 | cut -c1-150 ;;
  e1)  ctest_run "E1 tests (allocation, hashing, chain, invariants)" "E1" ;;
  e1-worked)
    header "E1 worked example and rounding example"
    echo "\$ ctest --test-dir build -R \"E1Allocation.(WorkedExample|RoundingExample|TiesGoToLowestId)\" -V"
    ctest --test-dir build -R "E1Allocation.(WorkedExample|RoundingExample|TiesGoToLowestId)" -V 2>&1 | grep -E "RUN|OK \]|PASSED|tests passed" ;;
  e2) cpp_db; ctest_run "E2 C++ tests (atomic sale, rollback, tamper, accounts)" "E2Test" ;;
  e3) cpp_db; ctest_run "E3 queue and concurrency tests" "E3Queue|ConcurrencyTest" ;;
  e4) cpp_db; ctest_run "E4 tests (server, roles, sign-up, market prices)" "E4Test" ;;
  oversell)
    cpp_db
    header "50 buyers by 20 runs: protected path against the unprotected comparison"
    echo "\$ ctest --test-dir build -R \"Protected_Fifty|Unprotected\" -V"
    ctest --test-dir build -R "Protected_Fifty|Unprotected" -V 2>&1 | grep -E "O6|\[       OK|PASSED" ;;
  all-cpp)
    cpp_db
    header "All C++ tests"
    echo "\$ ctest --test-dir build"
    ctest --test-dir build 2>&1 | tail -n 6 ;;
  js)
    header "JavaScript tests"
    echo "\$ node --test tests/js/"
    node --test tests/js/ 2>&1 | grep -E "^# (tests|pass|fail|duration_ms)" ;;
  static)
    header "Static checks"
    echo "\$ scripts/static_checks.sh"
    scripts/static_checks.sh ;;
  screens)
    header "Screen checks: every role at 1200 px and 390 px (Android phone profile)"
    echo "\$ scripts/screens_test.sh all"
    out=$(scripts/screens_test.sh all 2>&1 | grep -v trace-warn)
    echo "$out" | grep -E "^(FAIL|PASS)" | awk '{c[$1]++} END {printf "%d screen checks PASS, %d FAIL\n", c["PASS"], c["FAIL"]}'
    echo "$out" | grep -E "^FAIL|All screen|problem" ;;
  e2e)
    header "End-to-end story at 1200 px and 390 px"
    echo "\$ scripts/e2e_test.sh"
    scripts/e2e_test.sh 2>&1 | grep -v trace-warn ;;
  api)
    header "Real requests and responses from the running server (http://127.0.0.1:8080)"
    B=http://127.0.0.1:8080; J='Content-Type: application/json'
    curl -s -o /dev/null --max-time 3 $B/login.html || { echo "The server is not running. Start it in another terminal: scripts/run_server.sh"; exit 1; }
    show() { echo "\$ $1"; shift; "$@"; echo; echo; }
    show 'POST /auth/login (valid buyer)' curl -s -w '   [HTTP %{http_code}]' -c /tmp/ev_b.txt -X POST $B/auth/login -H "$J" -d '{"username":"buyer.1","password":"Demo-pass-2026"}'
    show 'POST /auth/login (wrong password)' curl -s -w '   [HTTP %{http_code}]' -X POST $B/auth/login -H "$J" -d '{"username":"buyer.1","password":"wrong"}'
    show 'POST /pools/1/integrity-check as a buyer' curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_b.txt -X POST $B/pools/1/integrity-check
    show 'POST /pools/1/sales with a negative quantity' curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_b.txt -X POST $B/pools/1/sales -H "$J" -d '{"request_id":"x","quantity_g":-5}'
    curl -s -c /tmp/ev_m.txt -X POST $B/auth/login -H "$J" -d '{"username":"manager","password":"Demo-pass-2026"}' >/dev/null
    POOL=$(curl -s -b /tmp/ev_m.txt $B/pools | python3 -c 'import sys,json;p=[x for x in json.load(sys.stdin) if x["status"]=="open" and x["stock_g"]>0];print(p[0]["pool_id"],p[0]["price_paise_per_kg"],p[0]["min_order_g"])')
    set -- $POOL; ID=$(python3 -c 'import uuid;print(uuid.uuid4())')
    BODY="{\"request_id\":\"$ID\",\"quantity_g\":$3,\"confirmed_price_paise_per_kg\":$2}"
    show "POST /pools/$1/sales (a real purchase of the minimum order)" curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_b.txt -X POST $B/pools/$1/sales -H "$J" -d "$BODY"
    show "the same request again (nothing sold twice)" curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_b.txt -X POST $B/pools/$1/sales -H "$J" -d "$BODY"
    show "POST /pools/$1/sales with a wrong price" curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_b.txt -X POST $B/pools/$1/sales -H "$J" -d "{\"request_id\":\"$(python3 -c 'import uuid;print(uuid.uuid4())')\",\"quantity_g\":$3,\"confirmed_price_paise_per_kg\":1}"
    show "POST /pools/$1/integrity-check as the Pool Manager" curl -s -w '   [HTTP %{http_code}]' -b /tmp/ev_m.txt -X POST $B/pools/$1/integrity-check
    echo "\$ GET /market-prices?commodity_id=1 as the Pool Manager (summary only)"
    curl -s -b /tmp/ev_m.txt "$B/market-prices?commodity_id=1" | python3 -c 'import sys,json;d=json.load(sys.stdin);print(json.dumps({k:d[k] for k in ("source","as_of","fetched_at","commodity")},indent=1))' ;;
  *)
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//' ;;
esac
