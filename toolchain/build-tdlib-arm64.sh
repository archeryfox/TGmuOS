#!/bin/bash
# Build TDLib (master) natively for AArch64 inside qemu-user container.
# fedora:39 = glibc 2.38, matching the muOS device (Buildroot glibc 2.38).
set -euo pipefail

TC=/toolchain
OUT=$TC/out
STATIC=$TC/static
mkdir -p "$OUT" "$STATIC"

if ! command -v g++ >/dev/null 2>&1; then
    dnf install -y -q gcc-c++ make cmake gperf python3 perl wget tar >/dev/null
fi
echo "=== toolchain ==="
g++ --version | head -1
ldd --version | head -1

# --- static zlib (cmake: ./configure runs test binaries, fragile under qemu) ---
if [ ! -f "$STATIC/lib/libz.a" ]; then
    cmake -S "$TC/zlib-1.3.1" -B "$TC/zlib-build" \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_INSTALL_PREFIX="$STATIC"
    cmake --build "$TC/zlib-build" -j"$(nproc)" --target install
    echo "=== zlib static done ==="
fi

# --- static OpenSSL ---
if [ ! -f "$STATIC/lib/libcrypto.a" ]; then
    cd "$TC"
    tar xzf openssl-3.3.2.tar.gz
    cd openssl-3.3.2
    ./Configure linux-aarch64 no-shared no-module no-docs --prefix="$STATIC" --openssldir="$STATIC/ssl" >/dev/null
    make -j"$(nproc)" >/dev/null && make install_sw >/dev/null
    echo "=== openssl static done ==="
fi

# --- TDLib ---
cmake -S "$TC/td-src" -B "$TC/td-build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DOPENSSL_ROOT_DIR="$STATIC" -DOPENSSL_USE_STATIC_LIBS=TRUE \
    -DZLIB_ROOT="$STATIC" \
    -DCMAKE_CXX_FLAGS="-static-libgcc -static-libstdc++" \
    -DCMAKE_C_FLAGS="-static-libgcc" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static-libgcc -static-libstdc++"
cmake --build "$TC/td-build" -j"$(nproc)" --target tdjson

SO=$(find "$TC/td-build" -name 'libtdjson.so*' -type f | head -1)
strip -s "$SO" -o "$OUT/libtdjson.so" 2>/dev/null || cp "$SO" "$OUT/libtdjson.so"

echo "=== result ==="
file "$OUT/libtdjson.so"
readelf -d "$OUT/libtdjson.so" | grep NEEDED || echo "no NEEDED (fully static deps)"
objdump -p "$OUT/libtdjson.so" | grep -o 'GLIBC_[0-9.]*' | sort -u
sha256sum "$OUT/libtdjson.so"
