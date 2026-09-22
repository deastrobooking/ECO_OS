#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
if [ "$#" -ne 1 ]; then
    echo "Usage: tools/render-demo.sh /absolute/path/demo.wav" >&2
    exit 1
fi
out="$(mktemp -d "${TMPDIR:-/tmp}/eco-render.XXXXXX")"
trap 'rm -rf "$out"' EXIT INT TERM
for source in native/light/*.c; do
    "${CC:-cc}" -std=c11 -O2 -I native/light -c "$source" -o "$out/$(basename "$source" .c).o"
done
"${CXX:-c++}" -std=c++20 -O2 -I native/src -I native/light native/src/render.cpp "$out"/*.o -lm -o "$out/eco-render"
"$out/eco-render" "$1"
