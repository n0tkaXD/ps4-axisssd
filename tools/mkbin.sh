#!/usr/bin/env bash
# Builds a ps4-payload-sdk ELF from C sources and wraps it into a raw .bin for
# GoldHEN BinLoader (bootstrap from ps4-payload-dev/elfldr).
# Usage: tools/mkbin.sh <out.bin> <cflags...> -- <sources...>
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export PS4_PAYLOAD_SDK="$ROOT/toolchain/ps4-payload-sdk"
export LLVM_CONFIG="$ROOT/toolchain/llvm/bin/llvm-config"
SDKB="$PS4_PAYLOAD_SDK/bin"
OUT="$1"; shift
FLAGS=()
while [ "$1" != "--" ]; do FLAGS+=("$1"); shift; done
shift
W="$(mktemp -d "$ROOT/proto/build/wrap.XXXX")"
"$SDKB/orbis-clang" -fPIC -mstackrealign -Wall -Werror -Os ${FLAGS[@]+"${FLAGS[@]}"} -o "$W/elfldr.elf" "$@"
"$SDKB/orbis-strip" "$W/elfldr.elf"
(cd "$W" && xxd -i elfldr.elf > elfldr_elf.c)
cp "$ROOT/toolchain/elfldr-src/bootstrap-bin.c" "$ROOT/toolchain/elfldr-src/bin_x86_64.x" "$W/"
"$SDKB/orbis-clang" -fPIC -mstackrealign -Wall -O1 -c "$W/bootstrap-bin.c" -o "$W/bootstrap-bin.o"
"$SDKB/orbis-ld" -T "$W/bin_x86_64.x" -o "$OUT" "$W/bootstrap-bin.o"
"$SDKB/orbis-objcopy" -O binary --only-section=.text "$OUT"
rm -rf "$W"
ls -l "$OUT"
