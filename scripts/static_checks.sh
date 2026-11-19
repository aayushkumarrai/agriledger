#!/bin/bash
# Static rules from spec §10 / CLAUDE.md: no floating point in engines/, no literal UI strings in web/.
cd "$(dirname "$0")/.."
fail=0
if grep -rnE '\b(float|double)\b|\b(atof|strtod|strtof|strtold|stod|stof|stold)\b' engines/ --include='*.cpp' --include='*.hpp' --include='*.h'; then
  echo "FAIL: floating-point type found in engines/ (CLAUDE.md rule 1)"; fail=1
else echo "PASS: no float/double in engines/"; fi
if [ -d web/js ] && grep -rnE "(textContent|innerText|innerHTML|alert\()[^;]*['\"][A-Za-z][A-Za-z ]{3,}['\"]" web/js 2>/dev/null; then
  echo "FAIL: literal UI string in web/js (CLAUDE.md rule 4)"; fail=1
else echo "PASS: no literal UI strings in web/js"; fi
exit $fail
