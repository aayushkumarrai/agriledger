#!/bin/bash
# Browser checks (Playwright + Chromium) against the real server: screenshots at 1200 px and 390 px (normal, greyscale,
# squint) and the audits for spacing, tokens, contrast, tap targets, number alignment, fonts, photos and icons.
# Output: tests/browser/out/. Needs a build in ./build. No database is needed (the pages are static files).
# Chromium's two missing system libraries are unpacked by scripts/setup_browser_libs.sh when they are not installed.
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/setup_browser_libs.sh >/dev/null
cmake --build build -j"$(nproc)" --target agriledger_server >/dev/null
exec node tests/browser/run.mjs
