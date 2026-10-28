#!/bin/bash
# Deploy fresh tgmuos binary to the Anbernic via adb.
# Usage: deploy-app.sh <path-to-aarch64-binary>
set -euo pipefail

BIN=${1:?usage: deploy-app.sh BINARY}
SERIES=bc001080a5104871f12
APP=/mnt/mmc/MUOS/application/tgmuos
TS=$(date +%Y%m%dT%H%M%S)

adb -s "$SERIES" shell "ps | grep '$APP/tgmuos' | grep -v grep" && {
    echo "tgmuos process is running; stopping it"
    adb -s "$SERIES" shell "pkill -f '$APP/tgmuos' || true"
    sleep 1
}

adb -s "$SERIES" shell "cp '$APP/tgmuos' '$APP/tgmuos.backup-dev-$TS'"
adb -s "$SERIES" push "$BIN" "$APP/tgmuos.new"
adb -s "$SERIES" shell "mv '$APP/tgmuos.new' '$APP/tgmuos' && chmod 755 '$APP/tgmuos' && md5sum '$APP/tgmuos'"
md5sum "$BIN"
