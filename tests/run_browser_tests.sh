#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
sdk="${FLIPPER_SDK_HEADERS:-$HOME/.ufbt/current/sdk_headers/f7_sdk}"
output="${TMPDIR:-/tmp}/tonie-browser-tests"
pkg=src/lib/file_browser
clang -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer \
 -Wno-unused-parameter -Wno-unused-function \
 -Itests/browser_stubs -I"$sdk/lib/mlib" -include tests/browser_stubs/furi.h \
 tests/test_browser.c "$pkg/helpers/fbp_cache.c" "$pkg/helpers/fbp_browser.c" \
 "$pkg/helpers/fbp_files.c" "$pkg/views/fbp_browser_view.c" -pthread -o "$output"
if [ "$#" -gt 0 ]; then
 "$output" "$1"
else
 fixture=$(python3 tests/make_browser_fixtures.py | python3 -c 'import sys,json; print(json.load(sys.stdin)["fixture"])')
 trap 'rm -rf "$fixture"' EXIT
 "$output" "$fixture"
fi
python3 tests/run-dates.py
