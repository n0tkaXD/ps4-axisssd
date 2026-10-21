#!/usr/bin/env bash
# Builds proto/build/ifusb_reg.bin (payload for GoldHEN BinLoader :9090) and
# runs the host check against the real-install snapshot first.
# Usage: proto/reg/build.sh [pkg path on console]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PKG="${1:-/user/data/pkg/IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg}"
EXTRA=()
[ "${NO_PROTECT:-}" = 1 ] && EXTRA+=(-DIFUSB_NO_PROTECT)
OUT=ifusb_reg
export PS4_PAYLOAD_SDK="$ROOT/toolchain/ps4-payload-sdk"
export LLVM_CONFIG="$ROOT/toolchain/llvm/bin/llvm-config"
SDKB="$PS4_PAYLOAD_SDK/bin"
SRC="$ROOT/proto/reg"
B="$ROOT/proto/build"
W="$B/reg_wrap"
mkdir -p "$B" "$W"

(cd "$ROOT/ps4mount/tools" && xxd -i -n k_pdb_template pdb_app_template.bin &&
    xxd -i -n k_pdb_patch_template pdb_patch_template.bin &&
    xxd -i -n k_pdb_addcont_template pdb_addcont_template.bin) |
    sed 's/^unsigned/static const unsigned/' > "$B/pdb_template.h"

LIB_SRCS=("$SRC/place.c" "$SRC/bgft.c" "$SRC/build.c" "$SRC/crypto.c" "$SRC/pkg.c")

echo "== host check"
cc -O1 -g -Wall -I"$B" -I"$SRC" -o "$B/host_test" "$SRC/host_test.c" "${LIB_SRCS[@]}"
rm -rf "$B/root" && mkdir -p "$B/root"
"$B/host_test" "$ROOT/testpkg/IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg" \
    /user/data/pkg/IV0000-AZIF00003_00-PAYLOADGUEST0000.pkg "$B/root" "$ROOT/snapshots/after/files" | tail -1

echo "== payload for $PKG"
"$SDKB/orbis-clang" -fPIC -mstackrealign -Wall -Werror -Os -I"$B" -I"$SRC" \
    -DIFUSB_PKG="\"$PKG\"" ${EXTRA[@]+"${EXTRA[@]}"} -o "$W/elfldr.elf" "$SRC/main.c" "${LIB_SRCS[@]}" -lSceSystemService
"$SDKB/orbis-strip" "$W/elfldr.elf"
(cd "$W" && xxd -i elfldr.elf > elfldr_elf.c)
cp "$ROOT/toolchain/elfldr-src/bootstrap-bin.c" "$ROOT/toolchain/elfldr-src/bin_x86_64.x" "$W/"
"$SDKB/orbis-clang" -fPIC -mstackrealign -Wall -O1 -c "$W/bootstrap-bin.c" -o "$W/bootstrap-bin.o"
"$SDKB/orbis-ld" -T "$W/bin_x86_64.x" -o "$B/$OUT.bin" "$W/bootstrap-bin.o"
"$SDKB/orbis-objcopy" -O binary --only-section=.text "$B/$OUT.bin"
ls -l "$B/$OUT.bin"
