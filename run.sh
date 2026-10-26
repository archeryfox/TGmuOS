#!/usr/bin/env bash
set -e

echo "=== tgmuos build script ==="

if [ ! -d build ]; then
    cmake -S . -B build
fi

cmake --build build

echo ""
echo "Build complete: ./build/tgmuos"
echo ""
echo "To run a headless host smoke test: ./build/tgmuos --smoke-test"
echo ""
echo "Run the TDLib JSON protocol test: ./build/tgmuos --telegram-json-test"
echo ""
echo "For Anbernic, cross-compile first; do not deploy the host build:"
echo "  scp build-muos/tgmuos root@192.168.0.151:/mnt/mmc/MUOS/application/tgmuos/tgmuos.next"
echo "  ssh root@192.168.0.151 \"mv /mnt/mmc/MUOS/application/tgmuos/tgmuos.next /mnt/mmc/MUOS/application/tgmuos/tgmuos\""
echo "Install AArch64 libtdjson.so in the app's lib/ directory and create telegram.conf locally."
echo ""
echo "The UI is drawn directly through /dev/fb0."
