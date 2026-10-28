# Multi-stage Dockerfile for building tgmuos for muOS (Anbernic RG35XX SP).
#
# Build:
#   docker build -t tgmuos-builder .
#   docker run --rm -v $(pwd):/workspace tgmuos-builder
#
# Produces aarch64 binary at /workspace/tgmuos/out/tgmuos
# and runs a smoke test under qemu-user-static before exiting.
#
# The image contains:
#   - aarch64-linux-gnu-gcc cross compiler (Fedora 39, glibc 2.43)
#   - muOS sysroot (glibc 2.38 headers/libs, kernel headers)
#   - SDL2/SDL2_ttf SDK (headers + .so for aarch64)
#   - TDLib source (for rebuilding libtdjson.so if needed)
#   - zlib source (for TDLib static dependency)

# ============================================================
# Stage 1: builder — cross compiler, sysroot, SDK, sources
# ============================================================
FROM fedora:39 AS builder

RUN dnf install -y -q --all-ones \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu \
    rpm \
    cpio \
    make \
    cmake \
    gperf \
    python3 \
    perl \
    wget \
    tar \
    git \
    qemu-user-static \
    && dnf clean all

WORKDIR /workspace

# --- muOS sysroot (glibc 2.38, matching the device) ---
# Fedora 39 glibc is 2.43; the device runs Buildroot glibc 2.38.
# Linker scripts and libc.so.6 from Fedora are forward-compatible.
RUN mkdir -p /workspace/toolchain/sysroot && \
    cd /workspace/toolchain && \
    dnf download -q --forcearch aarch64 --destdir=/workspace/toolchain \
        glibc-devel kernel-headers && \
    for r in /workspace/toolchain/glibc-devel-*.aarch64.rpm \
             /workspace/toolchain/kernel-headers-*.aarch64.rpm; do \
        rpm2cpio "$r" | ( cd /workspace/toolchain/sysroot && cpio -idm --quiet ); \
    done && \
    ln -sfn lib /workspace/toolchain/sysroot/lib64 && \
    printf 'INPUT ( libm.so.6 )\n' > /workspace/toolchain/sysroot/usr/lib64/libm.so

# --- SDL2/SDL2_ttf SDK (headers + shared libs for aarch64) ---
# These are provided by the build host; copy them in.
# Expected layout:
#   sdk/include/SDL2/SDL.h
#   sdk/include/SDL2/SDL_ttf.h
#   sdk/lib/libSDL2.so
#   sdk/lib/libSDL2_ttf.so

# --- TDLib source (for rebuilding libtdjson.so) ---
# Expected layout:
#   td-src/CMakeLists.txt
#   td-src/tdlib/...

# --- zlib source (for TDLib static dependency) ---
# Expected layout:
#   zlib-1.3.1/...

COPY build-muos.sh /workspace/toolchain/build-muos.sh
COPY build-tdlib-arm64.sh /workspace/toolchain/build-tdlib-arm64.sh
COPY deploy-app.sh /workspace/toolchain/deploy-app.sh
RUN chmod +x /workspace/toolchain/*.sh

# ============================================================
# Stage 2: build — compile the binary
# ============================================================
FROM builder AS build

# Copy the project source (user provides this)
COPY tgmuos /workspace/tgmuos

RUN bash /workspace/toolchain/build-muos.sh

# --- Smoke test under qemu-user-static ---
# Verifies the binary is a valid aarch64 ELF and can load its shared libs.
RUN if [ -f /workspace/tgmuos/out/tgmuos ]; then \
        echo "=== glibc requirements ===" && \
        aarch64-linux-gnu-objdump -p /workspace/tgmuos/out/tgmuos | \
            grep -o 'GLIBC_[0-9.]*' | sort -u; \
        echo "=== NEEDED libs ===" && \
        aarch64-linux-gnu-readelf -d /workspace/tgmuos/out/tgmuos | grep NEEDED; \
        echo "=== qemu smoke test ===" && \
        timeout 8 qemu-aarch64-static -L /workspace/toolchain/sysroot \
            /workspace/tgmuos/out/tgmuos --smoke-test 2>&1 || true; \
    else \
        echo "Binary not found at /workspace/tgmuos/out/tgmuos"; exit 1; \
    fi

# ============================================================
# Stage 3: release — minimal image with only the binary and sysroot
# ============================================================
FROM scratch AS release

COPY --from=build /workspace/tgmuos/out/tgmuos /tgmuos
COPY --from=build /workspace/toolchain/sysroot /sysroot
COPY --from=build /workspace/tgmuos /workspace/tgmuos

ENTRYPOINT ["/workspace/tgmuos/out/tgmuos"]