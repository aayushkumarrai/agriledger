#!/bin/bash
# Playwright's Chromium needs libnss3 and libnspr4. If they are not installed system-wide, this downloads the two .deb files
# (no root needed) and unpacks them into tests/browser/.libs, which the browser tests add to LD_LIBRARY_PATH.
# With root you can instead run:  sudo apt-get install libnss3 libnspr4
set -euo pipefail
cd "$(dirname "$0")/.."
if ldconfig -p | grep -q 'libnss3.so' && ldconfig -p | grep -q 'libnspr4.so'; then echo "libnss3 and libnspr4 are installed system-wide"; exit 0; fi
dest=tests/browser/.libs
if [ -f "$dest/root/usr/lib/x86_64-linux-gnu/libnss3.so" ]; then echo "already unpacked in $dest/root"; exit 0; fi
mkdir -p "$dest/debs"
( cd "$dest/debs" && apt-get download libnss3 libnspr4 >/dev/null )
for d in "$dest"/debs/*.deb; do dpkg -x "$d" "$dest/root"; done
echo "unpacked into $dest/root"
