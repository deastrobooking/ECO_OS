#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="$(mktemp -d "${TMPDIR:-/tmp}/eco-core.XXXXXX")"
trap 'rm -rf "$out"' EXIT INT TERM
neon=""
if [ "${ECO_FORCE_SCALAR:-0}" != 1 ] && { [ "$(uname -m)" = arm64 ] || [ "$(uname -m)" = aarch64 ]; }; then
  "${CC:-cc}" -c native/light/mix_neon.S -o "$out/mix_neon.o"
  neon="-DLIGHT_USE_NEON=1"
fi
for source in native/light/*.c; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined $neon \
    -I native/light -c "$source" -o "$out/$(basename "$source" .c).o"
done
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I native/src -I native/light native/tests/engine_test.cpp "$out"/*.o -o "$out/engine-tests"
"$out/engine-tests"
