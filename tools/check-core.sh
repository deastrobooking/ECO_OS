#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
out="$(mktemp -d "${TMPDIR:-/tmp}/eco-core.XXXXXX")"
trap 'rm -rf "$out"' EXIT INT TERM
# Sanitizers are part of the gate. Some hosts ship a broken ASan runtime (an
# empty program aborts); fall back loudly instead of failing every test.
san="-fsanitize=address,undefined"
echo 'int main(){return 0;}' > "$out/probe.c"
if ! "${CC:-cc}" $san "$out/probe.c" -o "$out/probe" 2>/dev/null || ! "$out/probe" 2>/dev/null; then
  echo "WARNING: sanitizer runtime unusable on this host; running WITHOUT ASan/UBSan" >&2
  san=""
fi
neon=""
if [ "${ECO_FORCE_SCALAR:-0}" != 1 ] && { [ "$(uname -m)" = arm64 ] || [ "$(uname -m)" = aarch64 ]; }; then
  "${CC:-cc}" -c native/light/mix_neon.S -o "$out/mix_neon.o"
  neon="-DLIGHT_USE_NEON=1"
fi
for source in native/light/*.c; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror $san $neon \
    -I native/light -c "$source" -o "$out/$(basename "$source" .c).o"
done
for source in native/src/devices/*.cpp; do
  "${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror $san \
    -I native/src -I native/light -I native/third_party/clap/include -c "$source" -o "$out/$(basename "$source" .cpp).o"
done
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror $san \
  -I native/src -I native/light -I native/third_party/clap/include -DECO_NO_ALLOC_GUARD native/tests/engine_test.cpp "$out"/*.o -o "$out/engine-tests"
"$out/engine-tests"
# Unsanitized pass: replaces operator new to prove render() never allocates.
for source in native/light/*.c; do
  "${CC:-cc}" -std=c11 -Wall -Wextra -Werror $neon -I native/light -c "$source" \
    -o "$out/plain_$(basename "$source" .c).o"
done
for source in native/src/devices/*.cpp; do
  "${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I native/src -I native/light \
    -I native/third_party/clap/include -c "$source" -o "$out/plain_$(basename "$source" .cpp).o"
done
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -I native/src -I native/light \
  -I native/third_party/clap/include native/tests/engine_test.cpp "$out"/plain_*.o \
  ${neon:+"$out/mix_neon.o"} -o "$out/engine-tests-alloc"
"$out/engine-tests-alloc"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror $san \
  -I native/src -I native/light -I native/third_party/clap/include native/tests/project_io_test.cpp -o "$out/project-io-tests"
"$out/project-io-tests"
