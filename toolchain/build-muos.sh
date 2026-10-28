#!/bin/bash
set -euo pipefail

# Build tgmuos for muOS (Anbernic RG35XX SP).
# Usage: build-muos.sh
# Expected layout:
#   /workspace/tgmuos           — project source (src/, include/, CMakeLists.txt)
#   /workspace/toolchain/sysroot — muOS sysroot (glibc + kernel headers)
#   /workspace/toolchain/sdk     — SDL2/SDL2_ttf SDK (include/, lib/)
#   /workspace/toolchain/out     — output directory

TC=/workspace/toolchain
SYS=$TC/sysroot
SDK=$TC/sdk
SRC=/workspace/tgmuos
OUT=$TC/out

if ! command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
    echo "aarch64-linux-gnu-gcc not found; install gcc-aarch64-linux-gnu"
    exit 1
fi

# --- Ensure sysroot exists ---
if [ ! -d "$SYS/usr/lib" ] || [ ! -f "$SYS/usr/include/stdio.h" ]; then
    echo "Sysroot missing; run Dockerfile setup or extract RPMs into $SYS"
    exit 1
fi

# --- Ensure SDL2 SDK exists ---
if [ ! -f "$SDK/include/SDL2/SDL.h" ] || [ ! -f "$SDK/include/SDL2/SDL_ttf.h" ]; then
    echo "SDL2 SDK missing at $SDK; copy SDL2/SDL2_ttf headers and .so there"
    exit 1
fi

# --- Fix libm linker script (muOS has no libmvec) ---
if [ ! -f "$SYS/usr/lib64/libm.so" ]; then
    printf 'INPUT ( libm.so.6 )\n' > "$SYS/usr/lib64/libm.so"
fi

# --- Fix lib64 symlink (Fedora linker scripts reference /lib64) ---
if [ ! -e "$SYS/lib64" ]; then
    ln -sfn lib "$SYS/lib64"
fi

mkdir -p "$OUT"
cd "$SRC"

echo "=== Building tgmuos ==="
aarch64-linux-gnu-gcc --sysroot="$SYS" -B"$SYS/usr/lib64" \
    -std=c11 -Wall -Wextra -Wpedantic \
    -Iinclude -I"$SDK/include" \
    src/*.c \
    -L"$SYS/usr/lib" -L"$SYS/usr/lib64" \
    -Wl,-rpath-link,"$SYS/usr/lib" -Wl,--allow-shlib-undefined \
    -lSDL2_ttf -lSDL2 -ldl -lm \
    -o "$OUT/tgmuos"

echo "=== Binary info ==="
aarch64-linux-gnu-readelf -h "$OUT/tgmuos" | head -3
echo "=== GLIBC refs ==="
aarch64-linux-gnu-objdump -p "$OUT/tgmuos" | grep -o 'GLIBC_[0-9.]*' | sort -u
echo "=== NEEDED ==="
aarch64-linux-gnu-readelf -d "$OUT/tgmuos" | grep NEEDED
ls -lh "$OUT/tgmuos"